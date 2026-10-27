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

static inline void _emit(const imm_vertex *v, bool tex1)
{
	_picaFixedAttribute(v->x, v->y, v->z, v->w);
	_picaFixedAttribute(v->r, v->g, v->b, v->a);
	_picaFixedAttribute(v->s0, v->t0, 0, 0);
	if (tex1)
		_picaFixedAttribute(v->s1, v->t1, 0, 0);
}

static void _emitQuadAround(const imm_vertex *a, const imm_vertex *b, float half, bool tex1)
{
	/* a thin quad along a->b, half-width 'half' in object units */
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

	_emit(&q[0], tex1); _emit(&q[1], tex1); _emit(&q[2], tex1);
	_emit(&q[0], tex1); _emit(&q[2], tex1); _emit(&q[3], tex1);
}

void glEnd(void)
{
	int n = imm_count;
	imm_count = 0;
	if (n <= 0) return;

	_stateFlush();

	bool tex1 = pglState->texUnitState[1];
	if (tex1)
		_picaAttribBuffersFormat(0, 0XFFFF, 0x3210, 4);
	else
		_picaAttribBuffersFormat(0, 0XFFFF, 0x210, 3);

	switch (imm_mode) {
		case GL_TRIANGLES:
			n -= n % 3;
			if (n <= 0) break;
			_picaImmediateBegin(GPU_TRIANGLES);
			for (int i = 0; i < n; i++) _emit(&imm_verts[i], tex1);
			_picaImmediateEnd();
			break;

		case GL_TRIANGLE_STRIP:
		case GL_QUAD_STRIP:
			if (n < 3) break;
			_picaImmediateBegin(GPU_TRIANGLE_STRIP);
			for (int i = 0; i < n; i++) _emit(&imm_verts[i], tex1);
			_picaImmediateEnd();
			break;

		case GL_TRIANGLE_FAN:
		case GL_POLYGON:
			if (n < 3) break;
			_picaImmediateBegin(GPU_TRIANGLE_FAN);
			for (int i = 0; i < n; i++) _emit(&imm_verts[i], tex1);
			_picaImmediateEnd();
			break;

		case GL_QUADS:
			n -= n % 4;
			if (n <= 0) break;
			_picaImmediateBegin(GPU_TRIANGLES);
			for (int i = 0; i < n; i += 4) {
				_emit(&imm_verts[i], tex1); _emit(&imm_verts[i + 1], tex1); _emit(&imm_verts[i + 2], tex1);
				_emit(&imm_verts[i], tex1); _emit(&imm_verts[i + 2], tex1); _emit(&imm_verts[i + 3], tex1);
			}
			_picaImmediateEnd();
			break;

		case GL_LINES:
			n -= n % 2;
			if (n <= 0) break;
			_picaImmediateBegin(GPU_TRIANGLES);
			for (int i = 0; i < n; i += 2) _emitQuadAround(&imm_verts[i], &imm_verts[i + 1], imm_line_width * 0.5f, tex1);
			_picaImmediateEnd();
			break;

		case GL_LINE_STRIP:
		case GL_LINE_LOOP:
			if (n < 2) break;
			_picaImmediateBegin(GPU_TRIANGLES);
			for (int i = 0; i + 1 < n; i++) _emitQuadAround(&imm_verts[i], &imm_verts[i + 1], imm_line_width * 0.5f, tex1);
			if (imm_mode == GL_LINE_LOOP && n > 2) _emitQuadAround(&imm_verts[n - 1], &imm_verts[0], imm_line_width * 0.5f, tex1);
			_picaImmediateEnd();
			break;

		case GL_POINTS:
			_picaImmediateBegin(GPU_TRIANGLES);
			for (int i = 0; i < n; i++) {
				imm_vertex a = imm_verts[i], b = imm_verts[i];
				a.x -= imm_point_size * 0.5f; b.x += imm_point_size * 0.5f;
				_emitQuadAround(&a, &b, imm_point_size * 0.5f, tex1);
			}
			_picaImmediateEnd();
			break;

		default:
			break;
	}

	if(++pglState->batchedDraws > MAX_BATCHED_DRAWS)
		glFlush();
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
