#include <math.h>
#include <stdlib.h>
#include <GL/gl.h>
#include <GL/glu.h>

void gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble near, GLdouble far)
{
	GLdouble xmin, xmax, ymin, ymax;

	ymax = near * tan( fovy * M_PI / 360.0 );
	ymin = -ymax;

	xmin = ymin * aspect;
	xmax = ymax * aspect;

	glFrustum( xmin, xmax, ymin, ymax, near, far );
}

void gluOrtho2D(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top)
{
	glOrtho(left, right, bottom, top, -1.0, 1.0);
}

void gluLookAt(GLdouble ex, GLdouble ey, GLdouble ez, GLdouble cx, GLdouble cy, GLdouble cz, GLdouble ux, GLdouble uy, GLdouble uz)
{
	float f[3] = { (float)(cx - ex), (float)(cy - ey), (float)(cz - ez) };
	float fl = sqrtf(f[0]*f[0] + f[1]*f[1] + f[2]*f[2]);
	f[0] /= fl; f[1] /= fl; f[2] /= fl;
	float up[3] = { (float)ux, (float)uy, (float)uz };
	float s[3] = { f[1]*up[2] - f[2]*up[1], f[2]*up[0] - f[0]*up[2], f[0]*up[1] - f[1]*up[0] };
	float sl = sqrtf(s[0]*s[0] + s[1]*s[1] + s[2]*s[2]);
	s[0] /= sl; s[1] /= sl; s[2] /= sl;
	float u[3] = { s[1]*f[2] - s[2]*f[1], s[2]*f[0] - s[0]*f[2], s[0]*f[1] - s[1]*f[0] };
	GLfloat m[16] = {
		s[0], u[0], -f[0], 0,
		s[1], u[1], -f[1], 0,
		s[2], u[2], -f[2], 0,
		0, 0, 0, 1 };
	glMultMatrixf(m);
	glTranslatef((float)-ex, (float)-ey, (float)-ez);
}

const GLubyte *gluErrorString(GLenum err) { (void)err; return (const GLubyte *)"no error"; }

struct GLUquadric { int dummy; };
GLUquadric *gluNewQuadric(void) { return (GLUquadric *)calloc(1, sizeof(GLUquadric)); }
void gluDeleteQuadric(GLUquadric *q) { free(q); }
void gluQuadricDrawStyle(GLUquadric *q, GLenum draw) { (void)q; (void)draw; }
void gluQuadricNormals(GLUquadric *q, GLenum normal) { (void)q; (void)normal; }
void gluQuadricTexture(GLUquadric *q, GLboolean texture) { (void)q; (void)texture; }
void gluQuadricCallback(GLUquadric *q, GLenum which, void (*fn)(void)) { (void)q; (void)which; (void)fn; }
void gluSphere(GLUquadric *q, GLdouble r, GLint sl, GLint st) { (void)q; (void)r; (void)sl; (void)st; }
void gluCylinder(GLUquadric *q, GLdouble b, GLdouble t, GLdouble h, GLint sl, GLint st) { (void)q; (void)b; (void)t; (void)h; (void)sl; (void)st; }
int gluBuild2DMipmaps(GLenum target, GLint internalFormat, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *data)
{
	glTexImage2D(target, 0, internalFormat, width, height, 0, format, type, data);
	return 0;
}
