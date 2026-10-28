#include "internal.h"
#include <math.h>

/*
 * Immediate mode (fs2-3ds rewrite).
 *
 * The PICA200 only rasterises triangles, so vertices between glBegin/glEnd are buffered
 * and converted at glEnd: quads and polygons become triangles, lines and points become
 * thin quads (sized in object units, which are pixels in 2D/ortho drawing).
 */

typedef struct {
	float x, y, z, w;
	float r, g, b, a;
	float s0, t0, s1, t1;
} imm_vertex;

#define IMM_MAX_VERTS 4096

static imm_vertex imm_verts[IMM_MAX_VERTS];
static int imm_count = 0;
static GLenum imm_mode = GL_TRIANGLES;
static float imm_line_width = 2.0f;
static float imm_point_size = 2.0f;

void glLineWidth(GLfloat width) { imm_line_width = (width < 1.0f ? 1.0f : width) * 2.0f; }
void glPointSize(GLfloat size) { imm_point_size = (size < 1.0f ? 1.0f : size) * 2.0f; }

void glBegin(GLenum mode)
{
	imm_mode = mode;
	imm_count = 0;
}

/*
 * fs2-3ds batching: glEnd no longer streams every vertex through the GPU command buffer
 * as fixed attributes. Vertices go into the (linear) geometry buffer as plain floats,
 * every primitive is turned into an indexed triangle list, and consecutive glBegin/glEnd
 * pairs that share the same GL state are merged into one draw call. A batch is closed
 * (drawn) as soon as any state changes (_stateFlush), before any other draw, clear,
 * target switch or flush. fs2_open's non-HTL path issues one glBegin/glEnd per polygon,
 * so this turns thousands of tiny draws per frame into a few dozen.
 */

static int       b_active = 0;
static bool      b_tex1 = false;
static int       b_stride = 10;          /* floats per vertex: 10 (pos, color, tc0) or 12 (+tc1) */
static float    *b_vbase = NULL;         /* first vertex of the open batch */
static uint16_t *b_ibase = NULL;         /* first index of the open batch */
static uint32_t  b_nverts = 0, b_nidx = 0;

#define B_MAX_VERTS 65000

pglStatsInternal pglStats;

static void _drawIndexedTriangles(uint32_t indexOffset, uint32_t count)
{
	GPUCMD_AddMaskedWrite(GPUREG_PRIMITIVE_CONFIG, 0x2, GPU_GEOMETRY_PRIM);
	GPUCMD_AddWrite(GPUREG_RESTART_PRIMITIVE, 1);
	GPUCMD_AddWrite(GPUREG_INDEXBUFFER_CONFIG, indexOffset | (1u << 31));   /* 16-bit indices */
	GPUCMD_AddWrite(GPUREG_NUMVERTICES, count);
	GPUCMD_AddWrite(GPUREG_VERTEX_OFFSET, 0);
	GPUCMD_AddMaskedWrite(GPUREG_GEOSTAGE_CONFIG, 0x2, 0x100);
	GPUCMD_AddMaskedWrite(GPUREG_GEOSTAGE_CONFIG2, 0x2, 0x100);
	GPUCMD_AddMaskedWrite(GPUREG_START_DRAW_FUNC0, 0x1, 0);
	GPUCMD_AddWrite(GPUREG_DRAWELEMENTS, 1);
	GPUCMD_AddMaskedWrite(GPUREG_START_DRAW_FUNC0, 0x1, 1);
	GPUCMD_AddWrite(GPUREG_VTX_FUNC, 1);
	GPUCMD_AddMaskedWrite(GPUREG_GEOSTAGE_CONFIG, 0x2, 0);
	GPUCMD_AddMaskedWrite(GPUREG_GEOSTAGE_CONFIG2, 0x2, 0);
}

void _batchSubmit(void)
{
	if (!b_active)
		return;
	b_active = 0;
	if (b_nidx == 0)
		return;

	uint64_t format = GPU_ATTRIBFMT(0, 4, GPU_FLOAT) | GPU_ATTRIBFMT(1, 4, GPU_FLOAT) | GPU_ATTRIBFMT(2, 2, GPU_FLOAT);
	uint64_t perm = 0x210;
	int nattr = 3;
	if (b_tex1) {
		format |= GPU_ATTRIBFMT(3, 2, GPU_FLOAT);
		perm = 0x3210;
		nattr = 4;
	}

	/* one interleaved buffer holding every attribute */
	uint32_t cfg[2];
	cfg[0] = (uint32_t)perm;
	cfg[1] = ((uint32_t)nattr << 28) | (((uint32_t)b_stride * 4) << 16);
	_picaAttribBufferOffset(0, (uint32_t)b_vbase - __ctru_linear_heap);
	_picaAttribBufferConfig(0, *(uint64_t*)cfg);
	for (int i = 1; i < 4; i++)
		_picaAttribBufferConfig(i, 0);   /* drop buffers left over from glDrawRangeElements */

	_picaAttribBuffersFormat(format, 0, perm, nattr);
	_drawIndexedTriangles((uint32_t)b_ibase - __ctru_linear_heap, b_nidx);

	_picaAttribBufferConfig(0, 0);       /* fixed-attribute draws (glClear) expect no buffers */

	pglStats.batches++;
	pglState->batchedDraws++;
}

static inline void _putVertex(float *d, const imm_vertex *v, bool tex1)
{
	d[0] = v->x; d[1] = v->y; d[2] = v->z; d[3] = v->w;
	d[4] = v->r; d[5] = v->g; d[6] = v->b; d[7] = v->a;
	d[8] = v->s0; d[9] = v->t0;
	if (tex1) { d[10] = v->s1; d[11] = v->t1; }
}

/* make room for nv vertices and ni indices in the open batch (opening one if needed) */
static void _batchReserve(int nv, int ni, bool tex1)
{
	int stride = tex1 ? 12 : 10;

	if (b_active && (b_tex1 != tex1 || b_nverts + nv > B_MAX_VERTS))
		_batchSubmit();

	uint32_t vbytes = (uint32_t)nv * stride * 4;
	uint32_t voff = b_active ? pglState->geometryBufferOffset : ((pglState->geometryBufferOffset + 15) & ~15u);
	bool cmdFull = gpuCmdBufOffset + 0x1000 > gpuCmdBufSize;

	if (voff + vbytes > GEOMETRY_BUFFER_SIZE || pglState->indexBufferOffset + ni + 8 > INDEX_BUFFER_SIZE / 2 || cmdFull)
	{
		_batchSubmit();
		glFlush();
		voff = pglState->geometryBufferOffset;
	}

	if (!b_active)
	{
		pglState->geometryBufferOffset = (voff + 15) & ~15u;
		pglState->indexBufferOffset = (pglState->indexBufferOffset + 7) & ~7u;   /* 16-byte aligned */
		b_vbase = (float*)((uint8_t*)pglState->geometryBuffer[pglState->geometryBufferCurrent] + pglState->geometryBufferOffset);
		b_ibase = pglState->indexBuffer[pglState->geometryBufferCurrent] + pglState->indexBufferOffset;
		b_nverts = 0;
		b_nidx = 0;
		b_tex1 = tex1;
		b_stride = stride;
		b_active = 1;
	}
}

static inline float *_vtxPtr(void)
{
	return b_vbase + b_nverts * b_stride;
}

static inline void _commit(int nv, int ni)
{
	b_nverts += nv;
	b_nidx += ni;
	pglState->geometryBufferOffset += nv * b_stride * 4;
	pglState->indexBufferOffset += ni;
	pglStats.verts += nv;
}

/* a thin quad along a->b, half-width 'half' in object units: 4 vertices, 6 indices */
static void _quadAround(const imm_vertex *a, const imm_vertex *b, float half, bool tex1)
{
	float dx = b->x - a->x, dy = b->y - a->y;
	float len = sqrtf(dx * dx + dy * dy);
	float nx, ny, ex, ey;
	if (len < 0.0001f) {
		nx = 0.0f; ny = half; ex = half; ey = 0.0f;
	} else {
		nx = -dy / len * half; ny = dx / len * half;
		ex = dx / len * half * 0.5f; ey = dy / len * half * 0.5f;
	}

	imm_vertex q[4] = { *a, *a, *b, *b };
	q[0].x = a->x - ex + nx; q[0].y = a->y - ey + ny;
	q[1].x = a->x - ex - nx; q[1].y = a->y - ey - ny;
	q[2].x = b->x + ex - nx; q[2].y = b->y + ey - ny;
	q[3].x = b->x + ex + nx; q[3].y = b->y + ey + ny;

	float *d = _vtxPtr();
	uint16_t base = (uint16_t)b_nverts;
	uint16_t *ix = b_ibase + b_nidx;
	for (int i = 0; i < 4; i++)
		_putVertex(d + i * b_stride, &q[i], tex1);
	ix[0] = base; ix[1] = base + 1; ix[2] = base + 2;
	ix[3] = base; ix[4] = base + 2; ix[5] = base + 3;
	_commit(4, 6);
}

void glEnd(void)
{
	int n = imm_count;
	imm_count = 0;
	if (n <= 0) return;

	pglStats.ends++;

	/* any pending state change closes the open batch, then gets applied */
	_stateFlush();

	bool tex1 = pglState->texUnitState[1];
	GLenum mode = imm_mode;

	switch (mode) {
		case GL_TRIANGLES:      n -= n % 3; if (n < 3) return; break;
		case GL_QUADS:          n -= n % 4; if (n < 4) return; break;
		case GL_TRIANGLE_STRIP:
		case GL_QUAD_STRIP:
		case GL_TRIANGLE_FAN:
		case GL_POLYGON:        if (n < 3) return; break;
		case GL_LINES:          n -= n % 2; if (n < 2) return; break;
		case GL_LINE_STRIP:
		case GL_LINE_LOOP:      if (n < 2) return; break;
		case GL_POINTS:         break;
		default:                return;
	}

	if (mode == GL_LINES || mode == GL_LINE_STRIP || mode == GL_LINE_LOOP || mode == GL_POINTS)
	{
		int segs = (mode == GL_LINES) ? n / 2 : (mode == GL_POINTS) ? n : (n - 1 + ((mode == GL_LINE_LOOP && n > 2) ? 1 : 0));
		float half = (mode == GL_POINTS ? imm_point_size : imm_line_width) * 0.5f;
		/* reserve in chunks so a huge line strip can't overflow one batch */
		for (int sgi = 0; sgi < segs; sgi++)
		{
			if ((sgi & 255) == 0) {
				int left = segs - sgi; if (left > 256) left = 256;
				_batchReserve(left * 4, left * 6, tex1);
			}
			if (mode == GL_POINTS) {
				imm_vertex a = imm_verts[sgi], b = imm_verts[sgi];
				a.x -= half; b.x += half;
				_quadAround(&a, &b, half, tex1);
			} else if (mode == GL_LINES) {
				_quadAround(&imm_verts[sgi * 2], &imm_verts[sgi * 2 + 1], half, tex1);
			} else {
				int i0 = sgi, i1 = (sgi + 1 < n) ? sgi + 1 : 0;
				_quadAround(&imm_verts[i0], &imm_verts[i1], half, tex1);
			}
		}
	}
	else
	{
		int ni;
		if (mode == GL_TRIANGLES) ni = n;
		else if (mode == GL_QUADS) ni = (n / 4) * 6;
		else ni = (n - 2) * 3;

		_batchReserve(n, ni, tex1);

		float *d = _vtxPtr();
		for (int i = 0; i < n; i++)
			_putVertex(d + i * b_stride, &imm_verts[i], tex1);

		uint16_t base = (uint16_t)b_nverts;
		uint16_t *ix = b_ibase + b_nidx;
		switch (mode) {
			case GL_TRIANGLES:
				for (int i = 0; i < n; i++) ix[i] = base + i;
				break;
			case GL_QUADS:
				for (int i = 0; i < n; i += 4) {
					*ix++ = base + i; *ix++ = base + i + 1; *ix++ = base + i + 2;
					*ix++ = base + i; *ix++ = base + i + 2; *ix++ = base + i + 3;
				}
				break;
			case GL_TRIANGLE_STRIP:
			case GL_QUAD_STRIP:
				for (int i = 0; i + 2 < n; i++) {
					if (i & 1) { *ix++ = base + i + 1; *ix++ = base + i; *ix++ = base + i + 2; }
					else       { *ix++ = base + i; *ix++ = base + i + 1; *ix++ = base + i + 2; }
				}
				break;
			default: /* fan / polygon */
				for (int i = 1; i + 1 < n; i++) {
					*ix++ = base; *ix++ = base + i; *ix++ = base + i + 1;
				}
				break;
		}
		_commit(n, ni);
	}

	if (pglState->batchedDraws > MAX_BATCHED_DRAWS)
	{
		_batchSubmit();
		glFlush();
	}
}

static inline void _immVertex(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
	if (imm_count >= IMM_MAX_VERTS) {
		/* split long batches (only safe for independent primitives) */
		GLenum mode = imm_mode;
		if (mode == GL_TRIANGLES || mode == GL_QUADS || mode == GL_LINES || mode == GL_POINTS) {
			glEnd();
			glBegin(mode);
		} else {
			return;
		}
	}
	imm_vertex *v = &imm_verts[imm_count++];
	v->x = x; v->y = y; v->z = z; v->w = w;
	v->r = pglState->currentColor.r; v->g = pglState->currentColor.g;
	v->b = pglState->currentColor.b; v->a = pglState->currentColor.a;
	v->s0 = pglState->currentTexCoord[0].s; v->t0 = pglState->currentTexCoord[0].t;
	v->s1 = pglState->currentTexCoord[1].s; v->t1 = pglState->currentTexCoord[1].t;
}

void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
	pglState->currentColor.r = red;
	pglState->currentColor.g = green;
	pglState->currentColor.b = blue;
	pglState->currentColor.a = alpha;
}

void glColor3f(GLfloat red, GLfloat green, GLfloat blue) { glColor4f(red, green, blue, 1.0f); }
void glColor3fv(const GLfloat *v) { glColor4f(v[0], v[1], v[2], 1.0f); }
void glColor3ub(GLubyte red, GLubyte green, GLubyte blue)
{
	glColor4f((1.0f/255.0f)*red, (1.0f/255.0f)*green, (1.0f/255.0f)*blue, 1.0f);
}
void glColor3ubv(const GLubyte* v)
{
	glColor4f((1.0f/255.0f)*v[0], (1.0f/255.0f)*v[1], (1.0f/255.0f)*v[2], 1.0f);
}
void glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha)
{
	glColor4f((1.0f/255)*red, (1.0f/255)*green, (1.0f/255)*blue, (1.0f/255)*alpha);
}
void glColor4ubv(const GLubyte* v)
{
	glColor4f((1.0f/255)*v[0], (1.0f/255)*v[1], (1.0f/255)*v[2], (1.0f/255)*v[3]);
}
void glColor4fv(const GLfloat* v) { glColor4f(v[0], v[1], v[2], v[3]); }

void glTexCoord2f(GLfloat s, GLfloat t)
{
	pglState->currentTexCoord[0].s = s;
	pglState->currentTexCoord[0].t = t;
}

void glTexCoord2fv(const GLfloat *v)
{
	pglState->currentTexCoord[0].s = v[0];
	pglState->currentTexCoord[0].t = v[1];
}

void glVertex2f(GLfloat x, GLfloat y) { _immVertex(x, y, 0.0f, 1.0f); }
void glVertex2i(GLint x, GLint y) { _immVertex((float)x, (float)y, 0.0f, 1.0f); }
void glVertex3f(GLfloat x, GLfloat y, GLfloat z) { _immVertex(x, y, z, 1.0f); }
void glVertex3fv(const GLfloat* v) { _immVertex(v[0], v[1], v[2], 1.0f); }
void glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w) { _immVertex(x, y, z, w); }

void glMultiTexCoord2f( GLenum target, GLfloat s, GLfloat t )
{
	uint8_t texunit = target & 0x01;

	pglState->currentTexCoord[texunit].s = s;
	pglState->currentTexCoord[texunit].t = t;
}

void glMultiTexCoord2fv( GLenum target, const GLfloat *v )
{
	glMultiTexCoord2f(target, v[0], v[1]);
}

void glArrayElement(GLint i)
{
	const void *vertexPointer  = pglState->vertexArrayPointer.pointer;
	const void *colorPointer   = pglState->colorArrayPointer.pointer;
	const void *texCoordPointer1 = pglState->texCoordArrayPointer[0].pointer;
	const void *texCoordPointer2 = pglState->texCoordArrayPointer[1].pointer;

	if(pglState->colorArrayState == GL_TRUE)
		glColor4ubv(colorPointer + (i * pglState->colorArrayPointer.stride));
	if(pglState->texCoordArrayState[0] == GL_TRUE)
		glTexCoord2fv(texCoordPointer1 + (i * pglState->texCoordArrayPointer[0].stride));
	if(pglState->texCoordArrayState[1] == GL_TRUE)
		glMultiTexCoord2fv(GL_TEXTURE1, texCoordPointer2 + (i * pglState->texCoordArrayPointer[1].stride));

	glVertex3fv(vertexPointer + (i * pglState->vertexArrayPointer.stride));
}
