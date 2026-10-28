#include "internal.h"

const GLubyte* glGetString(GLenum name)
{
	switch(name)
	{
		case GL_RENDERER:
			return (GLubyte*)"DMP(R) PICA200";
		case GL_VERSION:
			return (GLubyte*)"1.2";
		case GL_VENDOR:
			return (GLubyte*)"MasterFeizz";
		case GL_EXTENSIONS:
			return (GLubyte*)"GL_ARB_multitexture GL_ARB_texture_env_add GL_EXT_texture_env_add "
			                 "GL_EXT_draw_range_elements GL_EXT_bgra GL_ARB_texture_mirrored_repeat";
		default: 
			return (GLubyte*)"";
	}
}

#ifndef GL_MAX_TEXTURE_UNITS_ARB
#define GL_MAX_TEXTURE_UNITS_ARB 0x84E2
#endif
#ifndef GL_MAX_TEXTURE_IMAGE_UNITS_ARB
#define GL_MAX_TEXTURE_IMAGE_UNITS_ARB 0x8872
#endif
#ifndef GL_MAX_ELEMENTS_VERTICES
#define GL_MAX_ELEMENTS_VERTICES 0x80E8
#endif
#ifndef GL_MAX_ELEMENTS_INDICES
#define GL_MAX_ELEMENTS_INDICES 0x80E9
#endif
#ifndef GL_MAX_RENDERBUFFER_SIZE_EXT
#define GL_MAX_RENDERBUFFER_SIZE_EXT 0x84E8
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

#ifndef GL_MODELVIEW_STACK_DEPTH
#define GL_MODELVIEW_STACK_DEPTH 0x0BA3
#endif
#ifndef GL_PROJECTION_STACK_DEPTH
#define GL_PROJECTION_STACK_DEPTH 0x0BA4
#endif
#ifndef GL_STENCIL_BITS
#define GL_STENCIL_BITS 0x0D57
#endif

static GLenum _matrixMode(void)
{
	return (pglState->matrix_current == &pglState->matrix_projection) ? GL_PROJECTION : GL_MODELVIEW;
}

void glGetIntegerv(GLenum pname, GLint *params)
{
	if(!params) return;
	switch(pname)
	{
		case GL_MAX_TEXTURE_SIZE:            *params = 1024; break;
		case GL_MAX_LIGHTS:                  *params = 8; break;   /* engine lights on the CPU (-nohtl) */
		case GL_MAX_TEXTURE_UNITS_ARB:       *params = 2; break;
		case GL_MAX_TEXTURE_IMAGE_UNITS_ARB: *params = 2; break;
		case GL_MAX_ELEMENTS_VERTICES:       *params = 4096; break;
		case GL_MAX_ELEMENTS_INDICES:        *params = 8192; break;
		case GL_MAX_RENDERBUFFER_SIZE_EXT:   *params = 1024; break;
		case GL_MATRIX_MODE:                 *params = _matrixMode(); break;
		case GL_MODELVIEW_STACK_DEPTH:       *params = pglState->matrix_modelview_stack_counter + 1; break;
		case GL_PROJECTION_STACK_DEPTH:      *params = pglState->matrix_projection_stack_counter + 1; break;
		case GL_MAX_MODELVIEW_STACK_DEPTH:
		case GL_MAX_PROJECTION_STACK_DEPTH:  *params = MATRIX_STACK_SIZE; break;
		case GL_VIEWPORT:
			params[0] = 0; params[1] = 0;
			params[2] = pglState->targetWidth; params[3] = pglState->targetHeight;
			break;
		case GL_RED_BITS: case GL_GREEN_BITS: case GL_BLUE_BITS: case GL_ALPHA_BITS: case GL_STENCIL_BITS: *params = 8; break;
		case GL_DEPTH_BITS:                  *params = 24; break;
		default:                             *params = 0; break;
	}
}

static void _copyMatrix(const matrix4x4 *m, GLfloat *out)
{
	/* picaGL stores rows; GL wants column-major */
	for(int i = 0; i < 4; i++)
	{
		out[0 + i]  = m->row[i].x;
		out[4 + i]  = m->row[i].y;
		out[8 + i]  = m->row[i].z;
		out[12 + i] = m->row[i].w;
	}
}

void glGetFloatv(GLenum pname, GLfloat *params)
{
	if(!params) return;
	switch(pname)
	{
		case GL_MODELVIEW_MATRIX:  _copyMatrix(&pglState->matrix_modelview, params); break;
		case GL_PROJECTION_MATRIX: _copyMatrix(&pglState->matrix_projection, params); break;
		case GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT: *params = 1.0f; break;
		default: {
			GLint i[4] = {0, 0, 0, 0};
			glGetIntegerv(pname, i);
			params[0] = (GLfloat)i[0];
			break;
		}
	}
}