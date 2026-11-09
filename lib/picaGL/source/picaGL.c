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

	if(which == PGL_TARGET_OVERLAY)
	{
		if(!pglState->overlayColor)
		{
			size_t sz = pglState->mainGpuW * pglState->mainGpuH * 4;
			pglState->overlayColor = vramAlloc(sz);
			if(!pglState->overlayColor) pglState->overlayColor = linearAlloc(sz);
		}
		pglState->targetColor  = pglState->overlayColor;
		pglState->targetDepth  = NULL;
		pglState->targetWidth  = pglState->mainWidth;
		pglState->targetHeight = pglState->mainHeight;
		pglState->bufferGpuW   = pglState->mainGpuW;
		pglState->bufferGpuH   = pglState->mainGpuH;
	}
	else if(which == PGL_TARGET_MAIN)
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
		pglState->targetWidth  = bottom ? 320 : (pglState->topWide ? 800 : 400);
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

void pglOverlayAsTexture(GLuint name)
{
	if(!pglState->overlayColor)
	{
		size_t sz = pglState->mainGpuW * pglState->mainGpuH * 4;
		pglState->overlayColor = vramAlloc(sz);
		if(!pglState->overlayColor) pglState->overlayColor = linearAlloc(sz);
	}
	pglWrapTexture(name, pglState->overlayColor, pglState->mainGpuW, pglState->mainGpuH);
}

void pglSnapshotMain(GLuint name)
{
	static void *snap = NULL;
	size_t sz = pglState->mainGpuW * pglState->mainGpuH * 4;
	if(!snap) snap = linearAlloc(sz);
	if(!snap) return;

	/* everything drawn so far has to be in the target before it is copied */
	_batchSubmit();
	glFinish();
	GX_TextureCopy(pglState->mainColor, 0, snap, 0, sz, 8);
	_queueRun(false);

	pglWrapTexture(name, snap, pglState->mainGpuW, pglState->mainGpuH);
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
		/* watchdog: never hang on lost swap bookkeeping - force the swap after 250 ms */
		if (svcGetSystemTick() - t0 > (u64)(SYSCLOCK_ARM11 / 4)) {
			gxCmdQueueWait(&pglState->gxQueue, 50000000LL);
			if (!_swapPending) _swapPending = 1;
			_doPendingSwap();
			_swapsDone = _swapsRequested;
			pglStats.swapTimeouts++;
			break;
		}
	}
	{
		u64 tv = svcGetSystemTick();
		while (_vblankCount == _swapVblank && svcGetSystemTick() - tv < (u64)(SYSCLOCK_ARM11 / 20))
			gspWaitForEvent(GSPGPU_EVENT_VBlank0, true);
	}
	pglStats.swapWaitTicks += svcGetSystemTick() - t0;
}

static int _gfxWide = 0;

/* fs2-3ds: compose the top screen 800 wide from now on (call before binding PGL_TARGET_TOP
   for a frame). Returns whether wide mode is on (it needs the large top compose buffer). */
int pglSetTopWide(int wide)
{
	pglState->topWide = (wide && pglState->topWideCap) ? 1 : 0;
	return pglState->topWide;
}

void pglPresentScreensEx(int stereo, int bottom)
{
	_presentSetup();

	/* submit the frame (waits only for the previous command list) */
	glFlush();

	_waitPreviousSwap();

	/* the top screen's mode follows what this frame was composed for: 800x240 wide (2D, the
	   LCD's own columns) or the normal 400x240 per eye. The change takes effect with the swap. */
	if (pglState->topWide != _gfxWide)
	{
		_gfxWide = pglState->topWide;
		gfxSetWide(_gfxWide);
		gfxSet3D(!_gfxWide);
	}
	int topw = pglState->topWide ? 800 : 400;
	u32 *top = (u32*)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
	GX_DisplayTransfer(pglState->screenColor[0], GX_BUFFER_DIM(240, topw), top, GX_BUFFER_DIM(240, topw),
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
		out->swapTimeouts = pglStats.swapTimeouts;
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


/* save/restore the GL viewport + scissor rectangles across a compose pass
   (the engine's raw values; they are re-scaled for whatever target is bound) */
extern GLint pglViewportRaw[4], pglScissorRaw[4];
void _applyViewport(void);
void _applyScissor(void);
static GLint _sv_vp[4], _sv_sc[4];
static GLboolean _sv_sstate;
void pglSaveViewport(void)
{
	memcpy(_sv_vp, pglViewportRaw, sizeof(_sv_vp));
	memcpy(_sv_sc, pglScissorRaw, sizeof(_sv_sc));
	_sv_sstate = pglState->scissorState;
}
void pglRestoreViewport(void)
{
	memcpy(pglViewportRaw, _sv_vp, sizeof(_sv_vp));
	memcpy(pglScissorRaw, _sv_sc, sizeof(_sv_sc));
	pglState->scissorState = _sv_sstate;
	_applyViewport();
	_applyScissor();
	pglState->changes |= STATE_VIEWPORT_CHANGE | STATE_SCISSOR_CHANGE;
}
