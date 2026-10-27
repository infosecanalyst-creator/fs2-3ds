#include <stdio.h>
#include "internal.h"

static aptHookCookie _hookCookie;

static void _AptEventHook(APT_HookType type, void* param)
{

	switch (type)
	{
		case APTHOOK_ONSUSPEND:
		{
			_queueWaitAndClear();
			break;
		}
		case APTHOOK_ONRESTORE:
		{
			GX_BindQueue(&pglState->gxQueue);
			gxCmdQueueRun(&pglState->gxQueue);

			_picaRenderBuffer(pglState->targetColor, pglState->targetDepth);
			_picaAttribBuffersLocation((void*)__ctru_linear_heap);

			for(int i = 1; i < 6; i++)
				_picaTextureEnvSet(i, &pglState->texenv[PGL_TEXENV_DUMMY]);

			shaderProgramUse(&pglState->basicShader);
			pglState->changes |= 0xFFFFFFFF;
			break;
		}
		default:
			break;
	}
}

void pglInitEx(int mainWidth, int mainHeight);

void pglInit()
{
	pglInitEx(800, 480);
}

void pglInitEx(int mainWidth, int mainHeight)
{
	static int pgl_initialized = 0;

	if(pgl_initialized)
		return;

	pglState = malloc(sizeof(picaGLState));
	memset(pglState, 0, sizeof(picaGLState));
	pglState->mainWidth = mainWidth;
	pglState->mainHeight = mainHeight;
	
	_stateInitialize();
	_stateDefault();

	aptHook(&_hookCookie, _AptEventHook, NULL);
}

void pglExit()
{
	aptUnhook(&_hookCookie);

	_queueWaitAndClear();
	GX_BindQueue(NULL);

	//TODO: Clear memory
}

void pglSwapBuffers()
{
	glFlush();

	uint32_t *output_framebuffer = (uint32_t*)gfxGetFramebuffer(pglState->display, pglState->display_side, NULL, NULL);
	uint8_t output_format = gfxGetScreenFormat(pglState->display);
	
	if(pglState->display == GFX_TOP)
	{
		GX_DisplayTransfer(
			(u32*)pglState->colorBuffer, GX_BUFFER_DIM(240, 400),
			output_framebuffer, GX_BUFFER_DIM(240, 400),
			GX_TRANSFER_OUT_FORMAT(output_format));
	}
	else
	{
		GX_DisplayTransfer(
			(u32*)pglState->colorBuffer + (240*80),GX_BUFFER_DIM(240, 320),
			output_framebuffer, GX_BUFFER_DIM(240, 320),
			GX_TRANSFER_OUT_FORMAT(output_format));
	}
	
	_queueRun(false);

	gfxScreenSwapBuffers(pglState->display, false);
}

void pglSelectScreen(unsigned display, unsigned side)
{
	pglState->display = display;
	pglState->display_side = side;
}

/* ---------------- fs2-3ds additions ---------------- */

void pglBindTarget(int which)
{
	/* Make sure everything drawn to the previous target lands in memory */
	_picaFinalize(true);

	if(which == PGL_TARGET_MAIN)
	{
		pglState->targetColor  = pglState->mainColor;
		pglState->targetDepth  = pglState->mainDepth;
		pglState->targetWidth  = pglState->mainWidth;
		pglState->targetHeight = pglState->mainHeight;
		pglState->bufferGpuW   = pglState->mainGpuW;
		pglState->bufferGpuH   = pglState->mainGpuH;
	}
	else
	{
		int bottom = (which == PGL_TARGET_BOTTOM);
		pglState->targetColor  = pglState->screenColor[bottom];
		pglState->targetDepth  = NULL;
		pglState->targetWidth  = bottom ? 320 : 400;
		pglState->targetHeight = 240;
		pglState->bufferGpuW   = 240;
		pglState->bufferGpuH   = pglState->targetWidth;
	}

	pglState->colorBuffer = pglState->targetColor;
	pglState->depthBuffer = pglState->targetDepth;

	_picaRenderBuffer(pglState->targetColor, pglState->targetDepth);
	pglState->changes |= STATE_VIEWPORT_CHANGE | STATE_SCISSOR_CHANGE;
}

void pglGetTargetSize(int *w, int *h)
{
	if(w) *w = pglState->targetWidth;
	if(h) *h = pglState->targetHeight;
}

void pglWrapTexture(GLuint name, void *data, int gpuWidth, int gpuHeight);

void pglMainAsTexture(GLuint name)
{
	pglWrapTexture(name, pglState->mainColor, pglState->mainGpuW, pglState->mainGpuH);
}

void pglMainSize(int *w, int *h, int *gpuW, int *gpuH)
{
	if(w) *w = pglState->mainWidth;
	if(h) *h = pglState->mainHeight;
	if(gpuW) *gpuW = pglState->mainGpuW;
	if(gpuH) *gpuH = pglState->mainGpuH;
}

/* Send the composed top/bottom buffers to the LCDs. topRight: also fill the right-eye
   framebuffer from a second compose (pass NULL-equivalent 0 to copy left). */
void pglPresentScreens(int stereo)
{
	glFlush();

	u32 *top = (u32*)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
	GX_DisplayTransfer(pglState->screenColor[0], GX_BUFFER_DIM(240, 400), top, GX_BUFFER_DIM(240, 400),
		GX_TRANSFER_OUT_FORMAT(gfxGetScreenFormat(GFX_TOP)));
	_queueRun(false);

	if(stereo)
	{
		/* right eye was composed separately via pglPresentRightEye */
	}

	u32 *bot = (u32*)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
	GX_DisplayTransfer(pglState->screenColor[1], GX_BUFFER_DIM(240, 320), bot, GX_BUFFER_DIM(240, 320),
		GX_TRANSFER_OUT_FORMAT(gfxGetScreenFormat(GFX_BOTTOM)));
	_queueRun(false);

	gfxScreenSwapBuffers(GFX_TOP, stereo ? true : false);
	gfxScreenSwapBuffers(GFX_BOTTOM, false);
}

/* Copy the current top compose buffer into the right-eye framebuffer (call before
   composing the left eye into the same buffer, then pglPresentScreens(1)). */
void pglPresentRightEye(void)
{
	glFlush();
	u32 *right = (u32*)gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL);
	GX_DisplayTransfer(pglState->screenColor[0], GX_BUFFER_DIM(240, 400), right, GX_BUFFER_DIM(240, 400),
		GX_TRANSFER_OUT_FORMAT(gfxGetScreenFormat(GFX_TOP)));
	_queueRun(false);
}


/* save/restore the GL viewport + scissor rectangles across a compose pass */
static GLint _sv_vx, _sv_vy, _sv_vw, _sv_vh, _sv_sx, _sv_sy, _sv_sw, _sv_sh;
static GLboolean _sv_sstate;
void pglSaveViewport(void)
{
	_sv_vx = pglState->viewportX; _sv_vy = pglState->viewportY;
	_sv_vw = pglState->viewportWidth; _sv_vh = pglState->viewportHeight;
	_sv_sx = pglState->scissorX; _sv_sy = pglState->scissorY;
	_sv_sw = pglState->scissorWidth; _sv_sh = pglState->scissorHeight;
	_sv_sstate = pglState->scissorState;
}
void pglRestoreViewport(void)
{
	pglState->viewportX = _sv_vx; pglState->viewportY = _sv_vy;
	pglState->viewportWidth = _sv_vw; pglState->viewportHeight = _sv_vh;
	pglState->scissorX = _sv_sx; pglState->scissorY = _sv_sy;
	pglState->scissorWidth = _sv_sw; pglState->scissorHeight = _sv_sh;
	pglState->scissorState = _sv_sstate;
	pglState->changes |= STATE_VIEWPORT_CHANGE | STATE_SCISSOR_CHANGE;
}
