#include <string.h>
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
	_batchSubmit();
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

/*
 * fs2-3ds: asynchronous present.
 *
 * The old present waited for the GPU to finish the whole frame, then for each display
 * transfer, then for a VBlank - the CPU sat idle for the GPU's part of every frame and any
 * frame slower than 16.7 ms was rounded up to 33 ms. Now the frame's command list and the
 * two display transfers are queued and the CPU returns to the game at once; the screens
 * are swapped from the GX queue's completion callback, and the next present only waits
 * if the previous swap has not been shown yet (one VBlank since it). CPU and GPU overlap,
 * and the frame rate is no longer quantised to 60/30/20.
 */
static volatile u32 _vblankCount = 0;
static volatile u32 _swapVblank = 0;
static volatile int _swapPending = 0;      /* bit0 top, bit1 bottom, bit2 top stereo */
static volatile u32 _swapsRequested = 0, _swapsDone = 0;
static int _presentInit = 0;

static void _onVBlank(void *unused)
{
	(void)unused;
	_vblankCount++;
}

static void _doPendingSwap(void)
{
	int p = __atomic_exchange_n(&_swapPending, 0, __ATOMIC_SEQ_CST);
	if (!p)
		return;
	if (p & 1) gfxScreenSwapBuffers(GFX_TOP, (p & 4) != 0);
	if (p & 2) gfxScreenSwapBuffers(GFX_BOTTOM, false);
	_swapVblank = _vblankCount;
	__atomic_add_fetch(&_swapsDone, 1, __ATOMIC_SEQ_CST);
}

static void _onQueueDone(gxCmdQueue_s *q)
{
	(void)q;
	_doPendingSwap();
}

static void _presentSetup(void)
{
	if (_presentInit)
		return;
	_presentInit = 1;
	gspSetEventCallback(GSPGPU_EVENT_VBlank0, _onVBlank, NULL, false);
	gxCmdQueueSetCallback(&pglState->gxQueue, _onQueueDone, NULL);
}

/* wait until the previous frame has been swapped in and has been on screen since a VBlank,
   so its old back buffer (the one we are about to write) is no longer being scanned out */
static void _waitPreviousSwap(void)
{
	u64 t0 = svcGetSystemTick();
	while (_swapsDone != _swapsRequested)
	{
		if (gxCmdQueueWait(&pglState->gxQueue, 0))
			_doPendingSwap();          /* queue idle but callback raced us */
		else
			svcSleepThread(100000);
	}
	while (_vblankCount == _swapVblank)
		gspWaitForEvent(GSPGPU_EVENT_VBlank0, true);
	pglStats.swapWaitTicks += svcGetSystemTick() - t0;
}

void pglPresentScreensEx(int stereo, int bottom)
{
	_presentSetup();

	/* submit the frame (waits only for the previous command list) */
	glFlush();

	_waitPreviousSwap();

	u32 *top = (u32*)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
	GX_DisplayTransfer(pglState->screenColor[0], GX_BUFFER_DIM(240, 400), top, GX_BUFFER_DIM(240, 400),
		GX_TRANSFER_OUT_FORMAT(gfxGetScreenFormat(GFX_TOP)));

	if (bottom)
	{
		u32 *bot = (u32*)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
		GX_DisplayTransfer(pglState->screenColor[1], GX_BUFFER_DIM(240, 320), bot, GX_BUFFER_DIM(240, 320),
			GX_TRANSFER_OUT_FORMAT(gfxGetScreenFormat(GFX_BOTTOM)));
	}

	_swapsRequested++;
	__atomic_store_n(&_swapPending, 1 | (bottom ? 2 : 0) | (stereo ? 4 : 0), __ATOMIC_SEQ_CST);

	/* if the queue already drained before the flag was set, swap here */
	if (gxCmdQueueWait(&pglState->gxQueue, 0))
		_doPendingSwap();
}

void pglPresentScreens(int stereo)
{
	pglPresentScreensEx(stereo, 1);
}

void pglGetStats(pglFrameStats *out, int reset)
{
	if (out)
	{
		out->ends = pglStats.ends;
		out->batches = pglStats.batches;
		out->arrayDraws = pglStats.arrayDraws;
		out->flushes = pglStats.flushes;
		out->verts = pglStats.verts;
		out->flushWaitMs = (float)((double)pglStats.flushWaitTicks / (SYSCLOCK_ARM11 / 1000.0));
		out->swapWaitMs = (float)((double)pglStats.swapWaitTicks / (SYSCLOCK_ARM11 / 1000.0));
	}
	if (reset)
		memset(&pglStats, 0, sizeof(pglStats));
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
