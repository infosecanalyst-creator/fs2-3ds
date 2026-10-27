#include "internal.h"

const GLubyte* glGetString(GLenum name)
{
	switch(name)
	{
		case GL_RENDERER:
			return (GLubyte*)"DMP(R) PICA200";
		case GL_VERSION:
			return (GLubyte*)"1.1";
		case GL_VENDOR:
			return (GLubyte*)"MasterFeizz";
		case GL_EXTENSIONS:
			return (GLubyte*)"GL_ARB_multitexture GL_ARB_texture_env_add GL_EXT_texture_env_add "
			                 "GL_EXT_draw_range_elements GL_EXT_bgra GL_ARB_texture_mirrored_repeat";
		default: 
			return (GLubyte*)"";
	}
}

void glGetIntegerv(GLenum pname, GLint *params)
{
	switch(pname) 
	{
		case GL_MAX_TEXTURE_SIZE:
			*params = 1024;
	}
}