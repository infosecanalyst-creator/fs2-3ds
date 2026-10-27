#ifndef GLU_H_
#define GLU_H_

#include <GL/gl.h>

#ifdef __cplusplus
extern "C" {
#endif

void gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble near, GLdouble far);
void gluLookAt(GLdouble ex, GLdouble ey, GLdouble ez, GLdouble cx, GLdouble cy, GLdouble cz, GLdouble ux, GLdouble uy, GLdouble uz);
void gluOrtho2D(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top);
const GLubyte *gluErrorString(GLenum err);

/* quadrics: only used by debug/editor code; no-ops on the 3DS */
typedef struct GLUquadric GLUquadric;
typedef GLUquadric GLUquadricObj;
#define GLU_FILL    100012
#define GLU_NONE    100002
#define GLU_SMOOTH  100000
#define GLU_ERROR   100103
GLUquadric *gluNewQuadric(void);
void gluDeleteQuadric(GLUquadric *q);
void gluQuadricDrawStyle(GLUquadric *q, GLenum draw);
void gluQuadricNormals(GLUquadric *q, GLenum normal);
void gluQuadricTexture(GLUquadric *q, GLboolean texture);
void gluQuadricCallback(GLUquadric *q, GLenum which, void (*fn)(void));
void gluSphere(GLUquadric *q, GLdouble radius, GLint slices, GLint stacks);
void gluCylinder(GLUquadric *q, GLdouble base, GLdouble top, GLdouble height, GLint slices, GLint stacks);
int gluBuild2DMipmaps(GLenum target, GLint internalFormat, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *data);

#ifdef __cplusplus
}
#endif

#endif
