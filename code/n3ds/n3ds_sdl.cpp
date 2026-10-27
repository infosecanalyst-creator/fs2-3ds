/*
 * Minimal SDL 1.2 implementation for the FreeSpace 2 3DS port.
 *
 * The engine was written against SDL 1.2 for timers, threads, mutexes, joystick and
 * GL context handling. Rather than linking the full SDL port (which wants to own the
 * screens), we implement exactly the subset the engine uses on top of libctru and
 * picaGL. The real SDL 1.2 headers from portlibs are used so types and constants match.
 */
#include <3ds.h>
#include <GL/picaGL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include "SDL.h"
#include "n3ds_platform.h"

extern "C" {

/* ------------------------------------------------------------------ core */

static char sdl_error[256] = "";

int SDL_Init(Uint32 flags) { (void)flags; return 0; }
int SDL_InitSubSystem(Uint32 flags) { (void)flags; return 0; }
void SDL_QuitSubSystem(Uint32 flags) { (void)flags; }
Uint32 SDL_WasInit(Uint32 flags) { return flags; }
void SDL_Quit(void) { }
char *SDL_GetError(void) { return sdl_error; }
void SDL_SetError(const char *fmt, ...) { (void)fmt; }
void SDL_ClearError(void) { sdl_error[0] = 0; }

/* ------------------------------------------------------------------ time */

static u64 sdl_start_ticks = 0;

Uint32 SDL_GetTicks(void)
{
	if (!sdl_start_ticks) sdl_start_ticks = osGetTime();
	return (Uint32)(osGetTime() - sdl_start_ticks);
}

void SDL_Delay(Uint32 ms)
{
	svcSleepThread((s64)ms * 1000000LL);
}

/* ------------------------------------------------------------------ threads */

struct SDL_Thread {
	Thread th;
	int (*fn)(void *);
	void *data;
	int status;
};

static void sdl_thread_entry(void *arg)
{
	SDL_Thread *t = (SDL_Thread *)arg;
	t->status = t->fn(t->data);
}

SDL_Thread *SDL_CreateThread(int (*fn)(void *), void *data)
{
	SDL_Thread *t = (SDL_Thread *)calloc(1, sizeof(SDL_Thread));
	if (!t) return NULL;
	t->fn = fn;
	t->data = data;
	s32 prio = 0x30;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	/* run helper threads on the New 3DS extra core when possible */
	t->th = threadCreate(sdl_thread_entry, t, 64 * 1024, prio - 1, 2, false);
	if (!t->th) t->th = threadCreate(sdl_thread_entry, t, 64 * 1024, prio - 1, -2, false);
	if (!t->th) { free(t); return NULL; }
	return t;
}

void SDL_WaitThread(SDL_Thread *t, int *status)
{
	if (!t) return;
	threadJoin(t->th, U64_MAX);
	threadFree(t->th);
	if (status) *status = t->status;
	free(t);
}

void SDL_KillThread(SDL_Thread *t) { SDL_WaitThread(t, NULL); }

Uint32 SDL_ThreadID(void) { return (Uint32)(uintptr_t)threadGetCurrent(); }

struct SDL_mutex {
	RecursiveLock lock;
};

SDL_mutex *SDL_CreateMutex(void)
{
	SDL_mutex *m = (SDL_mutex *)malloc(sizeof(SDL_mutex));
	if (m) RecursiveLock_Init(&m->lock);
	return m;
}

void SDL_DestroyMutex(SDL_mutex *m) { free(m); }
int SDL_mutexP(SDL_mutex *m) { if (m) RecursiveLock_Lock(&m->lock); return 0; }
int SDL_mutexV(SDL_mutex *m) { if (m) RecursiveLock_Unlock(&m->lock); return 0; }

/* ------------------------------------------------------------------ timers */
/* The engine uses one periodic timer to service streamed audio. */

struct n3ds_timer {
	Thread th;
	volatile bool run;
	Uint32 interval;
	SDL_NewTimerCallback cb;
	void *param;
};

static void sdl_timer_entry(void *arg)
{
	n3ds_timer *t = (n3ds_timer *)arg;
	while (t->run) {
		svcSleepThread((s64)t->interval * 1000000LL);
		if (!t->run) break;
		Uint32 next = t->cb(t->interval, t->param);
		if (next == 0) break;
		t->interval = next;
	}
}

SDL_TimerID SDL_AddTimer(Uint32 interval, SDL_NewTimerCallback callback, void *param)
{
	n3ds_timer *t = (n3ds_timer *)calloc(1, sizeof(n3ds_timer));
	if (!t) return NULL;
	t->run = true;
	t->interval = interval ? interval : 1;
	t->cb = callback;
	t->param = param;
	s32 prio = 0x30;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	t->th = threadCreate(sdl_timer_entry, t, 32 * 1024, prio - 2, -2, false);
	if (!t->th) { free(t); return NULL; }
	return (SDL_TimerID)t;
}

SDL_bool SDL_RemoveTimer(SDL_TimerID id)
{
	n3ds_timer *t = (n3ds_timer *)id;
	if (!t) return SDL_FALSE;
	t->run = false;
	threadJoin(t->th, U64_MAX);
	threadFree(t->th);
	free(t);
	return SDL_TRUE;
}

/* ------------------------------------------------------------------ video */

static SDL_Surface sdl_video_surface;
static SDL_PixelFormat sdl_video_format;

SDL_Surface *SDL_SetVideoMode(int width, int height, int bpp, Uint32 flags)
{
	memset(&sdl_video_surface, 0, sizeof(sdl_video_surface));
	memset(&sdl_video_format, 0, sizeof(sdl_video_format));
	sdl_video_format.BitsPerPixel = 32;
	sdl_video_format.BytesPerPixel = 4;
	sdl_video_surface.format = &sdl_video_format;
	sdl_video_surface.w = width;
	sdl_video_surface.h = height;
	sdl_video_surface.flags = flags | SDL_FULLSCREEN;
	(void)bpp;
	return &sdl_video_surface;
}

SDL_Surface *SDL_GetVideoSurface(void) { return &sdl_video_surface; }

int SDL_GL_SetAttribute(SDL_GLattr attr, int value) { (void)attr; (void)value; return 0; }

int SDL_GL_GetAttribute(SDL_GLattr attr, int *value)
{
	switch (attr) {
		case SDL_GL_RED_SIZE: case SDL_GL_GREEN_SIZE: case SDL_GL_BLUE_SIZE: case SDL_GL_ALPHA_SIZE: *value = 8; break;
		case SDL_GL_DEPTH_SIZE: *value = 24; break;
		case SDL_GL_STENCIL_SIZE: *value = 8; break;
		case SDL_GL_DOUBLEBUFFER: *value = 1; break;
		default: *value = 0; break;
	}
	return 0;
}

void SDL_GL_SwapBuffers(void) { n3ds_present(); }
extern "C" void glActiveTextureARB(GLenum texture);
extern "C" void glClientActiveTextureARB(GLenum texture);

static void n3ds_glMultiTexCoord2fARB(GLenum target, GLfloat s, GLfloat t) { glMultiTexCoord2f(target, s, t); }

void *SDL_GL_GetProcAddress(const char *proc)
{
	static const struct { const char *name; void *fn; } table[] = {
		{ "glActiveTextureARB",       (void *)glActiveTextureARB },
		{ "glClientActiveTextureARB", (void *)glClientActiveTextureARB },
		{ "glMultiTexCoord2fARB",     (void *)n3ds_glMultiTexCoord2fARB },
		{ "glDrawRangeElements",      (void *)glDrawRangeElements },
		{ "glDrawRangeElementsEXT",   (void *)glDrawRangeElements },
	};
	for (unsigned i = 0; i < sizeof(table) / sizeof(table[0]); i++)
		if (!strcmp(proc, table[i].name)) return table[i].fn;
	return NULL;
}

int SDL_ShowCursor(int toggle) { (void)toggle; return 0; }
void SDL_WarpMouse(Uint16 x, Uint16 y) { (void)x; (void)y; }
SDL_GrabMode SDL_WM_GrabInput(SDL_GrabMode mode) { (void)mode; return SDL_GRAB_OFF; }
int SDL_WM_IconifyWindow(void) { return 0; }
void SDL_WM_SetCaption(const char *title, const char *icon) { (void)title; (void)icon; }
int SDL_WM_ToggleFullScreen(SDL_Surface *surface) { (void)surface; return 1; }
int SDL_SetGammaRamp(const Uint16 *r, const Uint16 *g, const Uint16 *b) { (void)r; (void)g; (void)b; return 0; }
int SDL_GetGammaRamp(Uint16 *r, Uint16 *g, Uint16 *b)
{
	for (int i = 0; i < 256; i++) {
		Uint16 v = (Uint16)(i << 8 | i);
		if (r) r[i] = v; if (g) g[i] = v; if (b) b[i] = v;
	}
	return 0;
}
int SDL_EnableKeyRepeat(int delay, int interval) { (void)delay; (void)interval; return 0; }

/* ------------------------------------------------------------------ events */
/* Input is fed straight into the engine from n3ds_input_poll(), so the SDL event
   queue is always empty. */

int SDL_PollEvent(SDL_Event *event) { (void)event; return 0; }
Uint8 SDL_EventState(Uint8 type, int state) { (void)type; return (Uint8)(state == SDL_QUERY ? SDL_ENABLE : state); }

static Uint8 sdl_keystate[SDLK_LAST];
Uint8 *SDL_GetKeyState(int *numkeys) { if (numkeys) *numkeys = SDLK_LAST; return sdl_keystate; }
SDLMod SDL_GetModState(void) { return KMOD_NONE; }

Uint8 SDL_GetMouseState(int *x, int *y)
{
	n3ds_mouse_pos(x, y);
	return (Uint8)n3ds_mouse_buttons();
}

/* ------------------------------------------------------------------ joystick */

struct _SDL_Joystick { int index; };
static struct _SDL_Joystick sdl_joy = { 0 };

int SDL_NumJoysticks(void) { return 1; }
const char *SDL_JoystickName(int index) { (void)index; return "New 3DS controls"; }
SDL_Joystick *SDL_JoystickOpen(int index) { (void)index; return &sdl_joy; }
void SDL_JoystickClose(SDL_Joystick *j) { (void)j; }
int SDL_JoystickNumAxes(SDL_Joystick *j) { (void)j; return 6; }
int SDL_JoystickNumBalls(SDL_Joystick *j) { (void)j; return 0; }
int SDL_JoystickNumHats(SDL_Joystick *j) { (void)j; return 1; }
int SDL_JoystickNumButtons(SDL_Joystick *j) { (void)j; return 16; }
int SDL_JoystickEventState(int state) { return state == SDL_QUERY ? SDL_ENABLE : state; }
void SDL_JoystickUpdate(void) { }
Sint16 SDL_JoystickGetAxis(SDL_Joystick *j, int axis) { (void)j; return (Sint16)n3ds_joy_axis(axis); }
Uint8 SDL_JoystickGetHat(SDL_Joystick *j, int hat) { (void)j; (void)hat; return (Uint8)n3ds_joy_hat(); }
Uint8 SDL_JoystickGetButton(SDL_Joystick *j, int button) { (void)j; return (Uint8)n3ds_joy_button(button); }

/* ------------------------------------------------------------------ RWops */

static int rw_stdio_seek(SDL_RWops *ctx, int offset, int whence)
{
	if (fseek(ctx->hidden.stdio.fp, offset, whence) == 0) return (int)ftell(ctx->hidden.stdio.fp);
	return -1;
}
static int rw_stdio_read(SDL_RWops *ctx, void *ptr, int size, int maxnum)
{
	return (int)fread(ptr, size, maxnum, ctx->hidden.stdio.fp);
}
static int rw_stdio_write(SDL_RWops *ctx, const void *ptr, int size, int num)
{
	return (int)fwrite(ptr, size, num, ctx->hidden.stdio.fp);
}
static int rw_stdio_close(SDL_RWops *ctx)
{
	if (ctx) {
		if (ctx->hidden.stdio.autoclose) fclose(ctx->hidden.stdio.fp);
		free(ctx);
	}
	return 0;
}

SDL_RWops *SDL_RWFromFP(FILE *fp, int autoclose)
{
	SDL_RWops *rw = (SDL_RWops *)calloc(1, sizeof(SDL_RWops));
	if (!rw) return NULL;
	rw->seek = rw_stdio_seek;
	rw->read = rw_stdio_read;
	rw->write = rw_stdio_write;
	rw->close = rw_stdio_close;
	rw->hidden.stdio.fp = fp;
	rw->hidden.stdio.autoclose = autoclose;
	return rw;
}

SDL_RWops *SDL_RWFromFile(const char *file, const char *mode)
{
	FILE *fp = fopen(file, mode);
	if (!fp) return NULL;
	return SDL_RWFromFP(fp, 1);
}

static int rw_mem_seek(SDL_RWops *ctx, int offset, int whence)
{
	Uint8 *pos;
	switch (whence) {
		case RW_SEEK_SET: pos = ctx->hidden.mem.base + offset; break;
		case RW_SEEK_CUR: pos = ctx->hidden.mem.here + offset; break;
		case RW_SEEK_END: pos = ctx->hidden.mem.stop + offset; break;
		default: return -1;
	}
	if (pos < ctx->hidden.mem.base) pos = ctx->hidden.mem.base;
	if (pos > ctx->hidden.mem.stop) pos = ctx->hidden.mem.stop;
	ctx->hidden.mem.here = pos;
	return (int)(pos - ctx->hidden.mem.base);
}
static int rw_mem_read(SDL_RWops *ctx, void *ptr, int size, int maxnum)
{
	int total = size * maxnum;
	int avail = (int)(ctx->hidden.mem.stop - ctx->hidden.mem.here);
	if (size <= 0) return 0;
	if (total > avail) total = (avail / size) * size;
	memcpy(ptr, ctx->hidden.mem.here, total);
	ctx->hidden.mem.here += total;
	return total / size;
}
static int rw_mem_write(SDL_RWops *ctx, const void *ptr, int size, int num)
{
	int total = size * num;
	int avail = (int)(ctx->hidden.mem.stop - ctx->hidden.mem.here);
	if (size <= 0) return 0;
	if (total > avail) total = (avail / size) * size;
	memcpy(ctx->hidden.mem.here, ptr, total);
	ctx->hidden.mem.here += total;
	return total / size;
}
static int rw_mem_close(SDL_RWops *ctx) { free(ctx); return 0; }

SDL_RWops *SDL_RWFromMem(void *mem, int size)
{
	SDL_RWops *rw = (SDL_RWops *)calloc(1, sizeof(SDL_RWops));
	if (!rw) return NULL;
	rw->seek = rw_mem_seek;
	rw->read = rw_mem_read;
	rw->write = rw_mem_write;
	rw->close = rw_mem_close;
	rw->hidden.mem.base = (Uint8 *)mem;
	rw->hidden.mem.here = (Uint8 *)mem;
	rw->hidden.mem.stop = (Uint8 *)mem + size;
	return rw;
}

void SDL_FreeRW(SDL_RWops *area) { free(area); }

/* ------------------------------------------------------------------ cpuinfo */

SDL_bool SDL_HasMMX(void) { return SDL_FALSE; }
SDL_bool SDL_HasSSE(void) { return SDL_FALSE; }
SDL_bool SDL_HasSSE2(void) { return SDL_FALSE; }
SDL_bool SDL_Has3DNow(void) { return SDL_FALSE; }
SDL_bool SDL_HasRDTSC(void) { return SDL_FALSE; }
SDL_bool SDL_HasAltiVec(void) { return SDL_FALSE; }

} /* extern "C" */

/* ------------------------------------------------------------------ entry point */
/* SDL_main.h renames the engine's main() to SDL_main(). */

extern "C" int SDL_main(int argc, char *argv[]);

int main(int argc, char *argv[])
{
	n3ds_init();

	/* Fixed command line suited to the hardware:
	   -nohtl      : CPU transform & lighting (picaGL has no GL lighting)
	   -noglow/-nospec/-noenv: skip extra texture passes
	   -res 800x480 handled by the platform layer through the registry defaults */
	static char *args[] = {
		(char *)"fs2_3ds",
		(char *)"-nohtl",
		(char *)"-noglow",
		(char *)"-nospec",
		(char *)"-noenv",
		(char *)"-nomotiondebris",
		(char *)"-no_vsync",
		NULL
	};
	int nargs = 0;
	while (args[nargs]) nargs++;

	(void)argc; (void)argv;
	int rc = SDL_main(nargs, args);

	n3ds_shutdown();
	return rc;
}
