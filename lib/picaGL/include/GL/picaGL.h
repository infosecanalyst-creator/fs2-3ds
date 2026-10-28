#ifndef __PICAGL_H__
#define __PICAGL_H__

#include <GL/gl.h>

#ifdef __cplusplus
extern "C" {
#endif

void pglInit();
void pglExit();
void pglSwapBuffers();
void pglSelectScreen(unsigned display, unsigned side);

/* fs2-3ds extensions */
#define PGL_TARGET_MAIN   0
#define PGL_TARGET_TOP    1
#define PGL_TARGET_BOTTOM 2
void pglInitEx(int mainWidth, int mainHeight);
void pglBindTarget(int which);
void pglGetTargetSize(int *w, int *h);
void pglMainAsTexture(GLuint name);
void pglMainSize(int *w, int *h, int *gpuW, int *gpuH);
void pglPresentScreens(int stereo);
void pglPresentScreensEx(int stereo, int bottom);   /* bottom = 0: leave the touchscreen as it is */

typedef struct {
	unsigned ends, batches, arrayDraws, flushes, verts;
	float flushWaitMs, swapWaitMs;
} pglFrameStats;
void pglGetStats(pglFrameStats *out, int reset);

/* scale applied to all viewports/scissors on the main target (1.0 or 0.5) */
void pglSetRenderScale(float scale);
float pglGetRenderScale(void);
void pglPresentRightEye(void);
void pglSaveViewport(void);
void pglRestoreViewport(void);

#ifdef __cplusplus
}
#endif

#endif