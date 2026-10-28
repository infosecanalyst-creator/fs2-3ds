#include "internal.h"

#include <stdio.h>

#include "texture_conv.inc"

static inline bool _addressIsVRAM(const void* addr)
{
	u32 vaddr = (u32)addr;
	return vaddr >= 0x1F000000 && vaddr < 0x1F600000;
}

static size_t _determineBPP(GPU_TEXCOLOR format)
{
	switch (format)
	{
		case GPU_RGBA8:  return 4;

		case GPU_RGB8:   return 3;

		case GPU_RGBA5551:
		case GPU_RGB565:
		case GPU_RGBA4:
		case GPU_LA8:    return 2;

		case GPU_A8:
		case GPU_LA4:    return 1;

		default:         return 4;
	}
}

static GPU_TEXCOLOR _determineHardwareFormat(GLenum format, GLenum srcFormat, GLenum srcType)
{
	/* 16-bit 1555 sources keep their 1-bit alpha exactly */
	if(srcType == GL_UNSIGNED_SHORT_1_5_5_5_REV)
		return GPU_RGBA5551;

	switch(format)
	{
		case GL_RGBA8: return GPU_RGBA8;
		case GL_RGB5_A1: return GPU_RGBA5551;

		case 4:
		case GL_RGBA:
		case GL_RGBA4: return GPU_RGBA4;

		case 3:
		case GL_RGB8:
		case GL_RGB:
		case GL_RGB5:  return GPU_RGB565;

		/* PICA samples A8 as (0,0,0,A); GL treats GL_ALPHA as (Cf, A) under MODULATE.
		   Store white luminance + alpha so text and HUD glyphs keep their colour. */
		case GL_ALPHA:
		case GL_ALPHA8: return GPU_LA8;
		case GL_LUMINANCE:
		case GL_LUMINANCE_ALPHA: return GPU_LA8;

		default: return GPU_RGBA4;
	}
}

static inline readFunc _determineReadFunction(GLenum format, GLenum type, uint8_t *bpp)
{
	switch (format) {
		case GL_ALPHA:
			switch (type) {
				case GL_UNSIGNED_BYTE: *bpp = 1; return _readA8;
				default:               return NULL;
			}
		case GL_LUMINANCE_ALPHA:
			switch (type) {
				case GL_UNSIGNED_BYTE: *bpp = 2; return _readLA8;
				default:               return NULL;
			}
		case GL_RGB:
			switch (type) {
				case GL_UNSIGNED_BYTE: *bpp = 3; return _readRGB8;
				default:               return NULL;
			}
		case GL_RGBA:
			switch (type) {
				case GL_UNSIGNED_BYTE: *bpp = 4; return _readRGBA8;
				default:               return NULL;
			}
		case GL_BGRA:
			switch (type) {
				case GL_UNSIGNED_BYTE:
				case GL_UNSIGNED_INT_8_8_8_8_REV: *bpp = 4; return _readBGRA8;
				case GL_UNSIGNED_SHORT_1_5_5_5_REV: *bpp = 2; return _readBGRA5551REV;
				default:               return NULL;
			}
		case GL_BGR:
			switch (type) {
				case GL_UNSIGNED_BYTE: *bpp = 3; return _readBGR8;
				default:               return NULL;
			}
		case GL_LUMINANCE:
			switch (type) {
				case GL_UNSIGNED_BYTE: *bpp = 1; return _readL8;
				default:               return NULL;
			}
		default:
			return NULL;
	}
}

static inline writeFunc _determineWriteFunction(GPU_TEXCOLOR format)
{
	switch (format) {
		case GPU_RGBA4:  return _writeRGBA4;
		case GPU_RGBA5551: return _writeRGBA5551;
		case GPU_RGBA8:  return _writeRGBA8;
		case GPU_RGB8:   return _writeRGB8;
		case GPU_RGB565: return _writeRGB565;
		case GPU_LA8:    return _writeLA8;
		case GPU_LA4:    return _writeLA4;
		case GPU_A8:     return _writeA8;
		default:         return NULL;
	}
}

static inline uint32_t _picaConvertWrap(uint32_t param)
{
	switch(param)
	{
		case GL_CLAMP:
		case GL_CLAMP_TO_EDGE: return GPU_CLAMP_TO_EDGE;
		case GL_REPEAT: return GPU_REPEAT;
		case GL_MIRRORED_REPEAT: return GPU_MIRRORED_REPEAT;
		default: return GPU_REPEAT;
	}
}

static inline uint32_t _picaConvertFilter(uint32_t param)
{
	switch(param)
	{	
		case GL_NEAREST_MIPMAP_NEAREST:
		case GL_NEAREST: return GPU_NEAREST;

		/* no mipmaps on this path: every *_MIPMAP_* filter and GL_LINEAR sample bilinear */
		default: return GPU_LINEAR;
	}
}

static inline void *_textureDataAlloc(size_t size)
{
	void *ret = NULL;

	if(size > 0x800)
		ret = vramAlloc(size);

	if(!ret)
		ret = linearAlloc(size); 

	return ret;
}

static inline void _textureDataFree(TextureObject *texture)
{
	if(texture->in_vram)
		vramFree(texture->data);
	else
		linearFree(texture->data);

	texture->data = NULL;
}

//Borrowed from citra
static inline uint32_t _mortonInterleave(uint32_t x, uint32_t y)
{
    static uint32_t xlut[] = {0x00, 0x01, 0x04, 0x05, 0x10, 0x11, 0x14, 0x15};
    static uint32_t ylut[] = {0x00, 0x02, 0x08, 0x0a, 0x20, 0x22, 0x28, 0x2a};

    return xlut[x % 8] + ylut[y % 8];
}

#define BLOCK_HEIGHT 8

static inline uint32_t _getMortonOffset(uint32_t x, uint32_t y)
{
	uint32_t coarse_x = x & ~7;

	u32 i = _mortonInterleave(x, y);

	uint32_t offset = coarse_x * BLOCK_HEIGHT;

	return (i + offset);
}

static inline void _textureTile(TextureObject *texture, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, const void* data, uint8_t bpp, readFunc readPixel, writeFunc writePixel)
{
	uint32_t texel, offset, output_y, coarse_y;

	void *tiled_output = texture->data;

	if(texture->in_vram)
	{
		tiled_output = linearAlloc(texture->width * texture->height * texture->bpp);

		if(!tiled_output)
			return;

		if(xoffset || yoffset)
		{
			_queueWaitAndClear();
			GX_TextureCopy(texture->data, 0, tiled_output, 0, texture->width * texture->height * texture->bpp, 8);
			_queueRun(false);
			
			_textureDataFree(texture);

			texture->data = tiled_output;
			texture->in_vram = false;
		}
	}

	/* fs2-3ds: direct loops for the formats fs2_open uploads most (no per-texel calls) */
	if(readPixel == _readBGRA5551REV && writePixel == _writeRGBA5551)
	{
		const uint16_t *src = (const uint16_t*)data;
		uint16_t *dst = (uint16_t*)tiled_output;
		for(int y = 0; y < height; y++)
		{
			output_y = texture->height - 1 - (y + yoffset);
			coarse_y = output_y & ~7;
			uint32_t row_base = coarse_y * texture->width;
			const uint16_t *s = src + y * width;
			for(int x = 0; x < width; x++)
			{
				uint16_t v = s[x];
				/* 1-5-5-5 (A R G B, A in bit 15) -> 5-5-5-1 (R G B A, A in bit 0) */
				dst[_getMortonOffset(x + xoffset, output_y) + row_base] = (uint16_t)((v << 1) | (v >> 15));
			}
		}
	}
	else if(readPixel == _readBGRA8 && writePixel == _writeRGBA8)
	{
		const uint8_t *src = (const uint8_t*)data;
		uint32_t *dst = (uint32_t*)tiled_output;
		for(int y = 0; y < height; y++)
		{
			output_y = texture->height - 1 - (y + yoffset);
			coarse_y = output_y & ~7;
			uint32_t row_base = coarse_y * texture->width;
			const uint8_t *s = src + y * width * 4;
			for(int x = 0; x < width; x++, s += 4)
			{
				/* B G R A bytes -> PICA RGBA8 word (A in low byte, then B, G, R) */
				dst[_getMortonOffset(x + xoffset, output_y) + row_base] =
					(uint32_t)s[3] | ((uint32_t)s[0] << 8) | ((uint32_t)s[1] << 16) | ((uint32_t)s[2] << 24);
			}
		}
	}
	else if(readPixel == _readA8 && writePixel == _writeLA8)
	{
		const uint8_t *src = (const uint8_t*)data;
		uint16_t *dst = (uint16_t*)tiled_output;
		for(int y = 0; y < height; y++)
		{
			output_y = texture->height - 1 - (y + yoffset);
			coarse_y = output_y & ~7;
			uint32_t row_base = coarse_y * texture->width;
			const uint8_t *s = src + y * width;
			for(int x = 0; x < width; x++)
				dst[_getMortonOffset(x + xoffset, output_y) + row_base] = (uint16_t)(s[x] | 0xFF00);
		}
	}
	else
	for(int y = 0; y < height; y++)
	{
		output_y = texture->height - 1 - (y + yoffset);
		coarse_y = output_y & ~7;

		for(int x = 0; x < width; x++)
		{
			offset = (_getMortonOffset(x + xoffset, output_y) + coarse_y * texture->width) * texture->bpp;
			texel = readPixel(data + ((x + (y * width)) * bpp));

			writePixel(tiled_output + offset, texel);
		}
	}

	GSPGPU_FlushDataCache(tiled_output, texture->width * texture->height * texture->bpp);

	if(texture->in_vram)
	{
		_queueWaitAndClear();
		GX_TextureCopy(tiled_output, 0, texture->data, 0, texture->width * texture->height * texture->bpp, 8);
		_queueRun(false);
		
		linearFree(tiled_output);
	}
}

void glActiveTexture(GLenum texture)
{
	uint8_t texunit = texture & 0x01;

	if(texunit > 1)
		texunit = 0;

	pglState->texUnitActive = texunit;
}

void glClientActiveTexture( GLenum texture )
{
	uint8_t texunit = texture & 0x01;

	if(texunit > 1)
		texunit = 0;

	pglState->texUnitActiveClient = texunit;
}

extern void n3ds_log(const char *fmt, ...);
static int _texUploads = 0;
static u64 _texUploadMs = 0;

void glTexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid* data)
{
	u64 _t0 = osGetTime();
	void _glTexImage2D_impl(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid*);
	_glTexImage2D_impl(target, level, internalFormat, width, height, border, format, type, data);
	u64 _dt = osGetTime() - _t0;
	_texUploads++;
	_texUploadMs += _dt;
	if (_dt >= 20 || (_texUploads % 100) == 0)
		n3ds_log("tex %dx%d fmt %x/%x took %d ms (total %d uploads, %d ms) linear free %d KB\n", width, height, format, type,
			(int)_dt, _texUploads, (int)_texUploadMs, (int)(linearSpaceFree() / 1024));
}

void _glTexImage2D_impl(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid* data)
{
	if(level != 0 || target != GL_TEXTURE_2D)
		return;

	TextureObject *texture = pglState->textureBound[pglState->texUnitActive];
	texture->used = true;

	if(texture->data)
		_textureDataFree(texture);

	texture->format = _determineHardwareFormat(internalFormat, format, type);
	texture->bpp 	= _determineBPP(texture->format);
	texture->width  = width;
	texture->height = height;
	texture->data   = _textureDataAlloc(width * height * texture->bpp);

	if(texture->data == NULL)
		return;

	if(_addressIsVRAM(texture->data))
		texture->in_vram = true;
	else
		texture->in_vram = false;

	pglState->textureChanged = GL_TRUE;
	pglState->changes |= STATE_TEXTURE_CHANGE;

	if(!data) return;

	uint8_t offset_bpp = 0;

	readFunc readPixel   = _determineReadFunction(format, type, &offset_bpp);
	writeFunc writePixel = _determineWriteFunction(texture->format);

	if(readPixel && writePixel && data)
		_textureTile(texture, 0, 0, width, height, data, offset_bpp, readPixel, writePixel);
}

void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid* data)
{
	if(level != 0 || target != GL_TEXTURE_2D || data == NULL)
		return;

	TextureObject *texture = pglState->textureBound[pglState->texUnitActive];

	if(texture->data == NULL)
		return;

	uint8_t offset_bpp = 0;

	readFunc readPixel   = _determineReadFunction(format, type, &offset_bpp);
	writeFunc writePixel = _determineWriteFunction(texture->format);

	if(readPixel && writePixel)
		_textureTile(texture, xoffset, yoffset, width, height, data, offset_bpp, readPixel, writePixel);

	pglState->textureChanged = GL_TRUE;
	pglState->changes |= STATE_TEXTURE_CHANGE;
}

void glTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
	TextureObject *texture = pglState->textureBound[pglState->texUnitActive];

	uint32_t tex_param = texture->param;

	switch(pname)
	{
		case GL_TEXTURE_MAG_FILTER:
			/* fs2-3ds: clear the MAG bit (bit 1) - the old mask cleared bit 0 */
			tex_param = (tex_param & ~GPU_TEXTURE_MAG_FILTER(1)) | GPU_TEXTURE_MAG_FILTER(_picaConvertFilter(param));
			break;
		case GL_TEXTURE_MIN_FILTER:
			/* fs2-3ds: clear the MIN bit (bit 2) - the old mask cleared the MAG bit, so every
			   texture ended up magnified with nearest-neighbour (blocky nebulae and planets) */
			tex_param = (tex_param & ~GPU_TEXTURE_MIN_FILTER(1)) | GPU_TEXTURE_MIN_FILTER(_picaConvertFilter(param));
			break;
		case GL_TEXTURE_WRAP_T:
			tex_param = (tex_param & ~GPU_TEXTURE_WRAP_T(3)) | GPU_TEXTURE_WRAP_T(_picaConvertWrap(param));
			break;
		case GL_TEXTURE_WRAP_S:
			tex_param = (tex_param & ~GPU_TEXTURE_WRAP_S(3)) | GPU_TEXTURE_WRAP_S(_picaConvertWrap(param));
			break;
		default:
			return;
	}

	texture->param = tex_param;

	pglState->changes |= STATE_TEXTURE_CHANGE;
}

void glTexParameteri(GLenum target, GLenum pname, GLint param)
{
	GLfloat fparam = (GLfloat)param;
	glTexParameterf( target, pname, fparam );
}

void glBindTexture(GLenum target, GLuint texture)
{
	TextureObject *texObject;

	texObject = hashTableGet(&pglState->textureTable, texture);

	if(texObject == NULL)
	{
		texObject = malloc(sizeof(TextureObject));

		texObject->param  = GPU_TEXTURE_MAG_FILTER(GPU_LINEAR) | GPU_TEXTURE_WRAP_S(GPU_REPEAT) | GPU_TEXTURE_WRAP_T(GPU_REPEAT);
		texObject->width  = 0;
		texObject->height = 0;

		texObject->format = 0;
		texObject->data   = NULL;
		texObject->in_vram = false;
		texObject->used = false;
		texObject->external = false;
		
		hashTableInsert(&pglState->textureTable, texture, texObject);
	}

	if(pglState->textureBound[pglState->texUnitActive] == texObject)
		return;

	pglState->textureBound[pglState->texUnitActive] = texObject;

	pglState->textureChanged = GL_TRUE;
	pglState->changes |= STATE_TEXTURE_CHANGE;
}

void glGenTextures(GLsizei n, GLuint *textures)
{
	TextureObject *texObject;
	int id;
	for(int i = 0; i < n; i++)
	{
		id = hashTableUniqueKey(&pglState->textureTable);
		texObject = malloc(sizeof(TextureObject));

		texObject->param  = GPU_TEXTURE_MAG_FILTER(GPU_LINEAR) | GPU_TEXTURE_WRAP_S(GPU_REPEAT) | GPU_TEXTURE_WRAP_T(GPU_REPEAT);
		texObject->width  = 0;
		texObject->height = 0;

		texObject->format = 0;
		texObject->data   = NULL;
		texObject->in_vram = false;
		texObject->used = false;
		texObject->external = false;

		hashTableInsert(&pglState->textureTable, id, texObject);

		textures[i] = id;
	}
}

void glDeleteTextures(GLsizei n, const GLuint *textures)
{
	for(int i = 0; i < n; i++)
	{
		TextureObject *texObject = hashTableRemove(&pglState->textureTable, textures[i]);

		if(!texObject)
			continue;

		/* fs2-3ds: never leave a dangling bound pointer */
		for(int u = 0; u < 2; u++)
		{
			if(pglState->textureBound[u] == texObject)
			{
				GLuint active = pglState->texUnitActive;
				pglState->texUnitActive = u;
				pglState->textureBound[u] = NULL;
				glBindTexture(GL_TEXTURE_2D, 0);
				pglState->texUnitActive = active;
			}
		}

		if(texObject->external)
		{
			free(texObject);
			continue;
		}

		if(texObject->data)
			_textureDataFree(texObject);

		free(texObject);
	}
}

GLboolean glIsTexture( GLuint texture ) 
{ 
	if(texture == 0)
		return GL_FALSE;

	TextureObject *texObject = hashTableGet(&pglState->textureTable, texture);

	if(texObject)
		return GL_TRUE;

	return GL_FALSE;
}

void glTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
	glTexImage2D( GL_TEXTURE_2D, level, internalformat, width, 1, border, format, type, pixels );
}

void glTexEnvfv( GLenum target, GLenum pname, const GLfloat *params )
{
	if ((target != GL_TEXTURE_ENV) || (pname != GL_TEXTURE_ENV_COLOR))
		return;

	uint8_t texunit = pglState->texUnitActive;
	TextureEnv *texenv  = &pglState->texenv[texunit];

	uint8_t r = (uint8_t)(params[0] * 255);
	uint8_t g = (uint8_t)(params[1] * 255);
	uint8_t b = (uint8_t)(params[2] * 255);
	uint8_t a = (uint8_t)(params[3] * 255);

	texenv->color = r | (g << 8) | (b << 16) | (a << 24);
	
	pglState->changes |= STATE_TEXTURE_CHANGE;
}

inline void glTexEnvi (GLenum target, GLenum pname, GLint param)
{
	if ((target != GL_TEXTURE_ENV) || (pname != GL_TEXTURE_ENV_MODE))
		return;
	
	uint8_t texunit = pglState->texUnitActive;
	TextureEnv *texenv  = &pglState->texenv[texunit];

	switch (param)
	{
	case GL_ADD:
		texenv->func_rgb = GPU_ADD;
		texenv->src_rgb  = GPU_TEVSOURCES(GPU_TEXTURE0 + texunit, texunit == 0 ? GPU_PRIMARY_COLOR : GPU_PREVIOUS, 0);
		texenv->op_rgb   = GPU_TEVOPERANDS(0, 0, 0);

		texenv->func_alpha = GPU_MODULATE;
		texenv->src_alpha  = GPU_TEVSOURCES(GPU_TEXTURE0 + texunit, texunit == 0 ? GPU_PRIMARY_COLOR : GPU_PREVIOUS, 0);
		texenv->op_alpha   = GPU_TEVOPERANDS(0, 0, 0);
		break;

	case GL_REPLACE:
		texenv->func_rgb = GPU_REPLACE;
		texenv->src_rgb  = GPU_TEVSOURCES(GPU_TEXTURE0 + texunit, 0, 0);
		texenv->op_rgb   = GPU_TEVOPERANDS(0, 0, 0);

		texenv->func_alpha = GPU_REPLACE;
		texenv->src_alpha  = GPU_TEVSOURCES(GPU_TEXTURE0 + texunit, 0, 0);
		texenv->op_alpha   = GPU_TEVOPERANDS(0, 0, 0);
		break;

	case GL_MODULATE:
		texenv->func_rgb = GPU_MODULATE;
		texenv->src_rgb  = GPU_TEVSOURCES(GPU_TEXTURE0 + texunit, texunit == 0 ? GPU_PRIMARY_COLOR : GPU_PREVIOUS, 0);
		texenv->op_rgb   = GPU_TEVOPERANDS(0, 0, 0);

		texenv->func_alpha = GPU_MODULATE;
		texenv->src_alpha  = GPU_TEVSOURCES(GPU_TEXTURE0 + texunit, texunit == 0 ? GPU_PRIMARY_COLOR : GPU_PREVIOUS, 0);
		texenv->op_alpha   = GPU_TEVOPERANDS(0, 0, 0);
		break;

	case GL_DECAL:
		texenv->func_rgb = GPU_INTERPOLATE;
		texenv->src_rgb  = GPU_TEVSOURCES(GPU_TEXTURE0 + texunit, texunit == 0 ? GPU_PRIMARY_COLOR : GPU_PREVIOUS, GPU_TEXTURE0 + texunit);
		texenv->op_rgb   = GPU_TEVOPERANDS(0, 0, GPU_TEVOP_RGB_SRC_ALPHA);

		texenv->func_alpha = GPU_REPLACE;
		texenv->src_alpha  = GPU_TEVSOURCES(GPU_PRIMARY_COLOR, 0, 0);
		texenv->op_alpha   = GPU_TEVOPERANDS(0, 0, 0);
		break;

	case GL_BLEND:
		texenv->func_rgb = GPU_INTERPOLATE;
		texenv->src_rgb  = GPU_TEVSOURCES(GPU_CONSTANT, texunit == 0 ? GPU_PRIMARY_COLOR : GPU_PREVIOUS,  GPU_TEXTURE0 + texunit);
		texenv->op_rgb   = GPU_TEVOPERANDS(0, 0, 0);

		texenv->func_alpha = GPU_MODULATE;
		texenv->src_alpha  = GPU_TEVSOURCES(GPU_TEXTURE0 + texunit, texunit == 0 ? GPU_PRIMARY_COLOR : GPU_PREVIOUS, 0);
		texenv->op_alpha   = GPU_TEVOPERANDS(0, 0, 0);
		break;

	default:
		break;
	}

	pglState->changes |= STATE_TEXTURE_CHANGE;
}

void glTexEnvf (GLenum target, GLenum pname, GLfloat param)
{
	glTexEnvi (target, pname, (int)param);
}

/* fs2-3ds: wrap externally owned GPU memory (e.g. the main render target) as a texture */
void pglWrapTexture(GLuint name, void *data, int gpuWidth, int gpuHeight)
{
	GLuint active = pglState->texUnitActive;
	pglState->texUnitActive = 0;
	glBindTexture(GL_TEXTURE_2D, name);
	TextureObject *t = pglState->textureBound[0];
	t->format = GPU_RGBA8;
	t->bpp = 4;
	t->width = gpuWidth;
	t->height = gpuHeight;
	t->data = data;
	t->size = gpuWidth * gpuHeight * 4;
	t->in_vram = true;
	t->used = true;
	t->external = true;
	t->param = GPU_TEXTURE_MAG_FILTER(GPU_LINEAR) | GPU_TEXTURE_MIN_FILTER(GPU_LINEAR) | GPU_TEXTURE_WRAP_S(GPU_CLAMP_TO_EDGE) | GPU_TEXTURE_WRAP_T(GPU_CLAMP_TO_EDGE);
	pglState->changes |= STATE_TEXTURE_CHANGE;
	pglState->texUnitActive = active;
}
