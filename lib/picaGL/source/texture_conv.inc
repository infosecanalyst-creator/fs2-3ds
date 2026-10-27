typedef uint32_t (*readFunc) (const void*);
typedef void 	 (*writeFunc)(void*, uint32_t);

uint32_t _readRGBA8(const void* data)
{
	return *(uint32_t*)data;
}

uint32_t _readRGB8(const void* data)
{
	uint8_t  *clr = (uint8_t*)data;

	uint8_t r = clr[0];
	uint8_t g = clr[1];
	uint8_t b = clr[2];
	uint8_t a = 255;

	return (r | (g << 8) | (b << 16) | (a << 24));
}

uint32_t _readLA8(const void* data)
{
	uint8_t  *clr = (uint8_t*)data;

	uint8_t l = clr[0];
	uint8_t a = clr[1];

	return (l | (l << 8) | (l << 16) | (a << 24));
}

uint32_t _readA8(const void* data)
{
	uint8_t  *clr = (uint8_t*)data;

	uint8_t l = 255;
	uint8_t a = clr[0];

	return (l | (l << 8) | (l << 16) | (a << 24));
}

void _writeRGBA4(void *data, uint32_t color)
{
	uint16_t *dst = (uint16_t*)data;
	uint8_t  *clr = (uint8_t*)&color;

	uint8_t r = clr[0] >> 4;
	uint8_t g = clr[1] >> 4;
	uint8_t b = clr[2] >> 4;
	uint8_t a = clr[3] >> 4;

	*dst = (a | (b << 4) | (g << 8) | (r << 12));
}

void _writeRGB565(void *data, uint32_t color)
{
	uint16_t *dst = (uint16_t*)data;
	uint8_t  *clr = (uint8_t*)&color;

	uint8_t r = clr[0] >> 3;
	uint8_t g = clr[1] >> 2;
	uint8_t b = clr[2] >> 3;

	*dst = (b | (g << 5) | (r << 11));
}

void _writeLA4(void *data, uint32_t color)
{
	uint8_t *dst = (uint8_t*)data;
	uint8_t *clr = (uint8_t*)&color;

	uint8_t l = clr[0] >> 4;
	uint8_t a = clr[3] >> 4;

	*dst = (a | (l << 4));
}

void _writeLA8(void *data, uint32_t color)
{
	uint16_t *dst = (uint16_t*)data;
	uint8_t  *clr = (uint8_t*)&color;

	uint8_t l = clr[0];
	uint8_t a = clr[3];

	*dst = (a | (l << 8));
}

void _writeA8(void *data, uint32_t color)
{
	uint8_t *dst = (uint8_t*)data;
	uint8_t *clr = (uint8_t*)&color;

	*dst = clr[3];
}


/* ---- fs2-3ds additions: formats used by fs2_open ---- */

/* GL_BGRA + GL_UNSIGNED_BYTE / GL_UNSIGNED_INT_8_8_8_8_REV (little endian: B,G,R,A bytes) */
uint32_t _readBGRA8(const void* data)
{
	const uint8_t *c = (const uint8_t*)data;
	return (c[2] | (c[1] << 8) | (c[0] << 16) | ((uint32_t)c[3] << 24));
}

/* GL_BGR + GL_UNSIGNED_BYTE */
uint32_t _readBGR8(const void* data)
{
	const uint8_t *c = (const uint8_t*)data;
	return (c[2] | (c[1] << 8) | (c[0] << 16) | (0xFFu << 24));
}

/* GL_BGRA + GL_UNSIGNED_SHORT_1_5_5_5_REV: bit15 A, 14-10 R, 9-5 G, 4-0 B */
uint32_t _readBGRA5551REV(const void* data)
{
	uint16_t v = *(const uint16_t*)data;
	uint32_t r = (v >> 10) & 0x1F, g = (v >> 5) & 0x1F, b = v & 0x1F;
	uint32_t a = (v & 0x8000) ? 0xFF : 0x00;
	r = (r << 3) | (r >> 2); g = (g << 3) | (g >> 2); b = (b << 3) | (b >> 2);
	return (r | (g << 8) | (b << 16) | (a << 24));
}

uint32_t _readL8(const void* data)
{
	uint8_t l = *(const uint8_t*)data;
	return (l | (l << 8) | (l << 16) | (0xFFu << 24));
}

void _writeRGBA5551(void *data, uint32_t color)
{
	uint16_t *dst = (uint16_t*)data;
	uint8_t  *clr = (uint8_t*)&color;
	uint16_t r = clr[0] >> 3, g = clr[1] >> 3, b = clr[2] >> 3, a = clr[3] >= 128 ? 1 : 0;
	*dst = (uint16_t)(a | (b << 1) | (g << 6) | (r << 11));
}

void _writeRGBA8(void *data, uint32_t color)
{
	uint8_t *dst = (uint8_t*)data;
	uint8_t *clr = (uint8_t*)&color;
	dst[0] = clr[3]; dst[1] = clr[2]; dst[2] = clr[1]; dst[3] = clr[0];
}

void _writeRGB8(void *data, uint32_t color)
{
	uint8_t *dst = (uint8_t*)data;
	uint8_t *clr = (uint8_t*)&color;
	dst[0] = clr[2]; dst[1] = clr[1]; dst[2] = clr[0];
}
