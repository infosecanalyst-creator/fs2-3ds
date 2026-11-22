/*
 * FreeSpace 2 for the New Nintendo 3DS - platform layer (input, screens, logging)
 */
#include <3ds.h>
#include <GL/picaGL.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <malloc.h>
#include <math.h>

/* libctru's KEY_* button enums clash with the engine's KEY_* scancode macros, so
   capture the button bits under other names before the engine headers are included */
static const u32 BTN_A = KEY_A, BTN_B = KEY_B, BTN_X = KEY_X, BTN_Y = KEY_Y;
static const u32 BTN_L = KEY_L, BTN_R = KEY_R, BTN_ZL = KEY_ZL, BTN_ZR = KEY_ZR;
static const u32 BTN_START = KEY_START, BTN_SELECT = KEY_SELECT, BTN_TOUCH = KEY_TOUCH;
static const u32 BTN_DUP = KEY_DUP, BTN_DDOWN = KEY_DDOWN, BTN_DLEFT = KEY_DLEFT, BTN_DRIGHT = KEY_DRIGHT;

#include "n3ds/n3ds_platform.h"
#include "n3ds/n3ds_panel.h"
#include "n3ds/n3ds_manual.h"
#include "n3ds/n3ds_dash.h"

#include "globalincs/pstypes.h"
#include "io/key.h"
#include "io/mouse.h"
#include "gamesequence/gamesequence.h"
#include "graphics/2d.h"
#include "pilotfile/pilotfile.h"
#include "gamesnd/gamesnd.h"
#include "sound/sound.h"
#include "popup/popup.h"
#include "playerman/managepilot.h"
#include "playerman/player.h"

#define FS2_DIR "sdmc:/3ds/freespace2"

/* ------------------------------------------------------------------ logging */

static FILE *n3ds_logfile = NULL;
static int n3ds_quit = 0;
static int n3ds_in_keyboard = 0;   /* the system keyboard applet is open (FS-05) */

/* FS-04: fs2_3ds.log used to get every line written and flushed to the SD card (one per
   button press, state change and model load, profiler lines every 300 frames), and each SD
   write stalls the game. Now n3ds_log() lines are written only when the debug log is switched
   on, buffered and rolled over to fs2_3ds.old.log at 1 MB. n3ds_note() lines (start, exit,
   pilot saved on close, crash, stall, failures) are always written and flushed at once: they
   are rare.
   The switch is START > SETTINGS > DEBUG LOG, off by default and kept like the other settings
   (a marker file, .debuglog). It used to be a debug.txt file in the game folder: one left on
   the card after a test kept the slow full log on for good with nothing on screen to say so.
   debug.txt is no longer read. */
static int n3ds_debug = -1;
static long n3ds_log_bytes = 0;

/* Settings are marker files in the game folder (.lefty, .swapped, .debuglog): present = on.
   Returns 1 when the card took the change. A failure (card full or locked) is noted in the log
   and remembered for the SETTINGS page; the setting still applies until the game is closed. */
static int n3ds_marker_fail = 0;
extern "C" void n3ds_note(const char *fmt, ...);
static int n3ds_marker_set(const char *path, int on)
{
	int ok;
	if (on) {
		FILE *f = fopen(path, "w");
		ok = f != NULL;
		if (f && fclose(f) != 0) ok = 0;
	} else {
		struct stat sb;
		ok = remove(path) == 0 || stat(path, &sb) != 0;      /* gone, or was never there */
	}
	n3ds_marker_fail = !ok;
	if (!ok) n3ds_note("setting not saved: could not %s %s\n", on ? "create" : "remove", path);
	return ok;
}
extern "C" int n3ds_marker_failed(void) { return n3ds_marker_fail; }

extern "C" int n3ds_debug_on(void)
{
	if (n3ds_debug < 0) {
		struct stat sb;
		n3ds_debug = (stat(FS2_DIR "/.debuglog", &sb) == 0) ? 1 : 0;
	}
	return n3ds_debug;
}

extern "C" void n3ds_set_debug(int on)
{
	n3ds_debug = on ? 1 : 0;
	n3ds_marker_set(FS2_DIR "/.debuglog", n3ds_debug);
	n3ds_note("debug log switched %s (START > SETTINGS)\n", n3ds_debug ? "on" : "off");
}

static void n3ds_log_v(int flush, const char *fmt, va_list ap)
{
	if (n3ds_logfile && n3ds_log_bytes > 1024 * 1024) {
		fclose(n3ds_logfile);
		n3ds_logfile = NULL;
		remove(FS2_DIR "/fs2_3ds.old.log");
		rename(FS2_DIR "/fs2_3ds.log", FS2_DIR "/fs2_3ds.old.log");
	}
	if (!n3ds_logfile) {
		static int opened_before = 0;
		/* first open of a run truncates; a reopen (after the quit path closed it) appends */
		n3ds_logfile = fopen(FS2_DIR "/fs2_3ds.log", opened_before ? "a" : "w");
		opened_before = 1;
		if (!n3ds_logfile) return;
		setvbuf(n3ds_logfile, NULL, _IOFBF, 16384);
		n3ds_log_bytes = 0;
	}
	static u64 t0 = 0;
	if (!t0) t0 = osGetTime();
	int n = fprintf(n3ds_logfile, "[%6.1f] ", (double)(osGetTime() - t0) / 1000.0);
	if (n > 0) n3ds_log_bytes += n;
	n = vfprintf(n3ds_logfile, fmt, ap);
	if (n > 0) n3ds_log_bytes += n;
	if (flush) fflush(n3ds_logfile);
}

extern "C" void n3ds_log(const char *fmt, ...)
{
	if (!n3ds_debug_on()) return;
	va_list ap;
	va_start(ap, fmt);
	n3ds_log_v(0, fmt, ap);
	va_end(ap);
}

extern "C" void n3ds_note(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	n3ds_log_v(1, fmt, ap);
	va_end(ap);
}

/* FS-07: memory headroom at mission load / end. Linear memory (32 MB here) holds picaGL's
   geometry, index and command buffers (~5.8 MB), the framebuffers and every texture that does
   not fit in VRAM; VRAM (6 MB) holds picaGL's render targets (~5 MB: 800x480 colour + depth in a
   512x1024 GPU surface each, both screens' compose buffers), leaving about 1 MB for textures.
   With debug.txt one line per call: free linear / VRAM now, the least free since the last line,
   texture memory in VRAM and in linear, heap free. Always (a note): free linear under 2 MB, or a
   texture that found no memory at all, since those end in missing textures or a crash. */
extern "C" char *fake_heap_end;
extern "C" void n3ds_mem_report(const char *tag)
{
	extern unsigned pgl_tex_vram_kb, pgl_tex_linear_kb, pgl_tex_failed, pgl_linear_low_kb, pgl_vram_low_kb;
	const unsigned lf = (unsigned)(linearSpaceFree() / 1024), vf = (unsigned)(vramSpaceFree() / 1024);
	struct mallinfo mi = mallinfo();
	const unsigned heap_free = (unsigned)((mi.fordblks + (fake_heap_end - (char *)sbrk(0))) / 1024);
	const unsigned llow = pgl_linear_low_kb < lf ? pgl_linear_low_kb : lf, vlow = pgl_vram_low_kb < vf ? pgl_vram_low_kb : vf;
	n3ds_log("mem %s: linear free %u KB (least %u), VRAM free %u KB (least %u), textures %u KB in VRAM + %u KB in linear, heap free %u KB, texture allocs failed %u\n",
	         tag, lf, llow, vf, vlow, pgl_tex_vram_kb, pgl_tex_linear_kb, heap_free, pgl_tex_failed);
	if (llow < 2048 || pgl_tex_failed)
		n3ds_note("LOW MEMORY at %s: linear free %u KB (least %u), %u texture allocs failed\n", tag, lf, llow, pgl_tex_failed);
	pgl_linear_low_kb = pgl_vram_low_kb = 0xFFFFFFFFu;
	pgl_tex_failed = 0;
}

extern "C" void n3ds_audio_shutdown(void);
extern "C" void n3ds_timers_stop(void);   /* n3ds_sdl.cpp */

/* FS-01/FS-02: every way out (Error()'s exit, the engine's exit(1)s, the quit door's return
   from SDL_main, HOME -> Close's _exit) stops our helper threads first: the streaming-audio
   timer threads (their callbacks feed the engine's audio streams) and the stall-trace thread
   (it writes to the log). Left running, they touched the log file, the AL state and the
   engine's audio streams while exit() closed and freed them. Runs once. */
static Thread n3ds_stall_th = NULL;           /* the stall-trace thread (n3ds_present) */
static volatile bool n3ds_stall_run = true;
static Thread n3ds_stall_thread_handle(void) { return n3ds_stall_th; }
extern "C" void n3ds_threads_stop(void)
{
	static bool done = false;
	if (done) return;
	done = true;
	Thread st = n3ds_stall_thread_handle();
	if (st) {
		n3ds_stall_run = false;
		if (R_SUCCEEDED(threadJoin(st, 2000000000ULL))) threadFree(st);
	}
	n3ds_timers_stop();
}

extern "C" void n3ds_fatal(const char *msg)
{
	if (n3ds_quit) _exit(0);     /* errors while closing down: just leave */
	n3ds_note("FATAL: %s\n", msg);
	errorConf err;
	errorInit(&err, ERROR_TEXT_WORD_WRAP, CFG_LANGUAGE_EN);
	errorText(&err, msg);
	errorDisp(&err);
	/* the caller ends the process next (exit, or abort for a failed assertion, which skips the
	   atexit handlers): stop the mixer and give our ndsp reference back first, so the DSP
	   thread is gone before the heap is (the timer and stall threads first, FS-01) */
	n3ds_threads_stop();
	n3ds_audio_shutdown();
}

/* crash handler: log the faulting address and registers, then show a dialog */
static u8 n3ds_exc_stack[0x2000] __attribute__((aligned(8)));
extern "C" { extern int __system_argc; extern char **__system_argv; }
static volatile int n3ds_test_run = 0;   /* started by the test agent (--selftest=...): see n3ds_exception_handler */
static char n3ds_exc_msg[512];

static void n3ds_exception_handler(ERRF_ExceptionInfo *excep, CpuRegisters *regs)
{
	snprintf(n3ds_exc_msg, sizeof(n3ds_exc_msg),
		"CRASH type %d  pc=%08lx lr=%08lx sp=%08lx far=%08lx\nr0=%08lx r1=%08lx r2=%08lx r3=%08lx\n",
		(int)excep->type, (unsigned long)regs->pc, (unsigned long)regs->lr, (unsigned long)regs->sp,
		(unsigned long)excep->far, (unsigned long)regs->r[0], (unsigned long)regs->r[1],
		(unsigned long)regs->r[2], (unsigned long)regs->r[3]);
	n3ds_note("%s", n3ds_exc_msg);
	if (n3ds_logfile) fclose(n3ds_logfile);
	n3ds_logfile = NULL;
	/* FS-10: a crash shows a crash screen and waits; it never restarts the console by itself.
	   (The Nov 16 version cold-restarted through the MCU so test runs never needed a hand: that
	   belongs to the test hooks' restartguard. A test run, started with --selftest=, keeps that
	   restart until restartguard owns it; a normal launch never does.) */
	if (n3ds_test_run) {
		svcSleepThread(1000000000LL);
		if (R_SUCCEEDED(mcuHwcInit())) {
			u8 v = 1 << 2;
			MCUHWC_WriteRegister(0x20, &v, 1);
			mcuHwcExit();
		}
		for (;;) svcSleepThread(1000000000LL);
	}
	PrintConsole top, bot;
	consoleInit(GFX_TOP, &top);
	consoleInit(GFX_BOTTOM, &bot);
	/* the console's font is 8x8: 50 columns on top, 40 below; every line is kept under that */
	consoleSelect(&top);
	printf("\n  FreeSpace 2 crashed\n\n");
	printf("  type %d   pc %08lx\n  lr %08lx   sp %08lx\n  far %08lx\n", (int)excep->type, (unsigned long)regs->pc,
	       (unsigned long)regs->lr, (unsigned long)regs->sp, (unsigned long)excep->far);
	consoleSelect(&bot);
	printf("\n The game stopped on an error.\n\n The details are in\n sdmc:/3ds/freespace2/\n fs2_3ds.log\n\n Press START to go back to the\n Homebrew Menu.\n");
	gfxFlushBuffers();
	gfxSwapBuffers();
	for (;;) {
		hidScanInput();
		if (hidKeysDown() & KEY_START) break;
		gspWaitForVBlank();
	}
	gfxExit();
	_exit(0);
}

int n3ds_trace_frames = 10;

extern "C" int n3ds_keyboard(int password, const char *initial, char *out, int outlen)
{
	n3ds_trace_frames = 20;
	n3ds_in_keyboard = 1;               /* the keyboard applet suspends us too: no pilot save then (FS-05) */
	SwkbdState kb;
	swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, outlen - 1);
	swkbdSetHintText(&kb, password ? "Password" : "Enter text");
	if (initial && initial[0]) swkbdSetInitialText(&kb, initial);
	if (password) swkbdSetPasswordMode(&kb, SWKBD_PASSWORD_HIDE_DELAY);
	swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
	SwkbdButton b = swkbdInputText(&kb, out, outlen);
	n3ds_in_keyboard = 0;
	n3ds_log("keyboard: button %d text '%s'\n", (int)b, (b == SWKBD_BUTTON_CONFIRM && !password) ? out : "");
	return b == SWKBD_BUTTON_CONFIRM;
}

/* ------------------------------------------------------------------ init */

static GLuint n3ds_main_tex = 0;
static GLuint n3ds_overlay_tex = 0;   /* panel / manual / dashboard are drawn here, never into the game's frame */
static GLuint n3ds_snap_tex = 0;      /* engine save_screen copy */

/* FS-06: FreeSpace 2 needs a New 3DS (its memory and 804 MHz CPU). On an original 3DS/2DS say
   so on both screens and go back to the Homebrew Menu, instead of running out of memory later. */
static void n3ds_require_new3ds(void)
{
	bool is_new = false;
	if (R_FAILED(APT_CheckNew3DS(&is_new)) || is_new) return;
	gfxInitDefault();
	PrintConsole top, bot;
	consoleInit(GFX_TOP, &top);
	consoleInit(GFX_BOTTOM, &bot);
	/* the console's font is 8x8: 50 columns on top, 40 below (every line here fits in 38) */
	static const char *const lines[] = {
		"FreeSpace 2 needs a New 3DS",
		"(New 3DS, New 3DS XL or New 2DS XL).",
		"",
		"This is an original 3DS or 2DS: it",
		"lacks the memory and CPU speed the",
		"game needs.",
		"",
		"Press START to go back.",
	};
	for (int s = 0; s < 2; s++) {
		consoleSelect(s ? &bot : &top);
		int cols = s ? 40 : 50;
		printf("\x1b[9;1H");
		for (unsigned i = 0; i < sizeof lines / sizeof lines[0]; i++)
			printf("%*s%s\n", (cols - (int)strlen(lines[i])) / 2, "", lines[i]);
	}
	while (aptMainLoop()) {
		hidScanInput();
		if (hidKeysDown() & (BTN_START | BTN_A | BTN_B)) break;
		gspWaitForVBlank();
	}
	gfxExit();
	_exit(0);
}

extern "C" void n3ds_init(void)
{
	n3ds_require_new3ds();
	osSetSpeedupEnable(true);           /* 804 MHz + L2 cache on New 3DS */
	for (int i = 0; i < __system_argc; i++)
		if (__system_argv[i] && !strncmp(__system_argv[i], "--selftest=", 11)) n3ds_test_run = 1;
	{ struct stat tst; if (stat("sdmc:/3ds/testagent/emu_selftest.txt", &tst) == 0) n3ds_test_run = 1; }
	threadOnException(n3ds_exception_handler, n3ds_exc_stack + sizeof(n3ds_exc_stack), WRITE_DATA_TO_HANDLER_STACK);
	atexit(n3ds_threads_stop);   /* FS-01: any exit() stops the helper threads (n3ds_al.cpp registers it again after the mixer's) */
	gfxInitDefault();
	gfxSet3D(true);
	hidInit();

	mkdir(FS2_DIR, 0777);
	chdir(FS2_DIR);

	n3ds_note("FreeSpace 2 3DS starting%s\n", n3ds_debug_on() ? " (debug log on: START > SETTINGS)" : "");
	n3ds_note("heap %lu KB, linear %lu KB free\n", (unsigned long)(envGetHeapSize() / 1024), (unsigned long)(linearSpaceFree() / 1024));

	pglInitEx(N3DS_GAME_W, N3DS_GAME_H);
	glGenTextures(1, &n3ds_main_tex);
	pglMainAsTexture(n3ds_main_tex);
	glGenTextures(1, &n3ds_overlay_tex);
	pglOverlayAsTexture(n3ds_overlay_tex);
	glGenTextures(1, &n3ds_snap_tex);
	glBindTexture(GL_TEXTURE_2D, 0);
	pglBindTarget(PGL_TARGET_MAIN);
	n3ds_log("graphics ready\n");
}

extern "C" void n3ds_shutdown(void)
{
	n3ds_threads_stop();   /* FS-02: the quit door returns from SDL_main to here */
	n3ds_note("shutdown\n");
	if (n3ds_logfile) fclose(n3ds_logfile);
	n3ds_logfile = NULL;
	pglExit();
	hidExit();
	gfxExit();
}

extern "C" int n3ds_should_quit(void) { return n3ds_quit; }

/* ------------------------------------------------------------------ input */

/* virtual joystick */
static int joy_axes[6];
static int joy_buttons[16];
static int joy_hat = 0;

/* touch / mouse */
static int touch_x = N3DS_GAME_W / 2, touch_y = N3DS_GAME_H / 2;
static int touch_down = 0;

extern "C" int n3ds_joy_axis(int axis) { return (axis >= 0 && axis < 6) ? joy_axes[axis] : 0; }
extern "C" int n3ds_joy_button(int b) { return (b >= 0 && b < 16) ? joy_buttons[b] : 0; }
extern "C" int n3ds_joy_hat(void) { return joy_hat; }

extern "C" void n3ds_mouse_pos(int *x, int *y)
{
	if (x) *x = touch_x;
	if (y) *y = touch_y;
}

extern "C" int n3ds_mouse_buttons(void) { return touch_down ? 1 : 0; }

static int in_flight(void)
{
	return gameseq_get_state() == GS_STATE_GAME_PLAY;
}

/* map a 3DS button to an engine key in the current context */
struct keymap { u32 button; int flight_key; int menu_key; };

static const keymap n3ds_keys[] = {
	/* button        in flight                     in menus     */
	{ BTN_A,       KEY_TAB,                    0 },             /* afterburner / mouse click (below) */
	{ BTN_B,       KEY_X,                      KEY_ESC },     /* countermeasure / back */
	{ BTN_X,       KEY_Y,                      KEY_ENTER },   /* target ship in reticle / accept */
	{ BTN_Y,       KEY_T,                      0 },             /* target next ship */
	{ BTN_R,       KEY_LCTRL,                  0 },             /* fire primary */
	{ BTN_L,       KEY_SPACEBAR,               0 },             /* fire secondary */
	{ BTN_DUP,     KEY_H,                      KEY_UP },      /* nearest hostile */
	{ BTN_DDOWN,   KEY_M,                      KEY_DOWN },    /* match target speed */
	{ BTN_DLEFT,   KEY_PERIOD,                 KEY_LEFT },    /* cycle primary */
	{ BTN_DRIGHT,  KEY_DIVIDE,                 KEY_RIGHT },   /* cycle secondary bank */
	{ BTN_START,   KEY_ESC,                    KEY_ESC },     /* in-game menu */
	{ BTN_SELECT,  KEY_C,                      0 },             /* comms (wingman orders) */
	{ BTN_ZR,      KEY_A,                      0 },             /* throttle up (hold) */
};

/* hold SELECT + button for secondary functions in flight */
static const keymap n3ds_select_keys[] = {
	{ BTN_Y,       KEY_F,                      0 },             /* next friendly */
	{ BTN_X,       KEY_S,                      0 },             /* next subsystem */
	{ BTN_A,       KEY_Q,                      0 },             /* equalize shields */
	{ BTN_DUP,     KEY_R,                      0 },             /* closest attacker */
	{ BTN_DDOWN,   KEY_B,                      0 },             /* next bomb */
	{ BTN_DLEFT,   KEY_F3,                     0 },             /* target-view toggle */
	{ BTN_DRIGHT,  KEY_PADPLUS,                0 },             /* radar range */
	{ BTN_ZR,      KEY_Z,                      0 },             /* throttle down (hold) */
};

static int key_held[256];

static void send_key(int key, int down)
{
	if (key <= 0) return;
	int idx = key & 0xff;
	if (down && key_held[idx]) return;
	if (!down && !key_held[idx]) return;
	key_held[idx] = down;
	key_mark(key, down ? 1 : 0, 0);
}

static void release_all_keys(void)
{
	for (int i = 0; i < 256; i++) {
		if (key_held[i]) {
			key_mark(i, 0, 0);
			key_held[i] = 0;
		}
	}
}

static int scale_axis(int v, int range)
{
	/* circle pad reports about +-156, C-stick about +-146 */
	int s = v * 32767 / range;
	if (s > 32767) s = 32767;
	if (s < -32767) s = -32767;
	return s;
}

/* ---- menu view on the top screen: zoom levels show 400x240, 533x320 or the whole
   800x480 engine frame; the view scrolls to keep the pointer on screen */
static const float Menu_zooms[3] = { 0.5f, 0.75f, 1.0f };   /* LCD pixels per engine pixel */
static int menu_zoom = 0;                                   /* start with the whole frame filling the top screen; L/R or ZOOM to zoom in (Ryan: zoom only on a button press) */
static float view_x = 0, view_y = 0;

static float menu_zoom_scale(void) { return Menu_zooms[menu_zoom]; }

/* ZL swaps the screens: the cockpit / menu picture goes on the touchscreen (it fills it), the
   panel / dashboard / manual buttons go on the top screen (the dashboard fills it; the cockpit
   panel is centred). The stylus is then the mouse on the menu picture. */
static int swapped = 0;
int n3ds_screens_swapped(void) { return swapped; }

/* FS2-U2: with the screens swapped the menu is on the touchscreen. Zoom level 0 shows the whole
   800x480 frame on it (as before: every item can be tapped, but small text cannot be read);
   L / R / ZOOM now zoom this view too: level 1 shows 427x320 and level 2 shows 320x240 engine
   pixels (1:1, as sharp as the top screen's closest zoom). The view follows the pointer when the
   Circle Pad or C-Stick moves it; it never moves because of the stylus, so a tap lands on what
   is under the stylus and the picture stays put afterwards. Returns the size of the view in
   engine pixels. */
static int view_follow = 1;         /* the pointer was last moved by a stick (0: put there by the stylus) */
static void menu_view_size(float *ww, float *wh)
{
	if (swapped) {
		if (menu_zoom == 0) { *ww = N3DS_GAME_W; *wh = N3DS_GAME_H; }
		else { *ww = 320.0f / menu_zoom_scale(); *wh = 240.0f / menu_zoom_scale(); }
	} else {
		*ww = 400.0f / menu_zoom_scale(); *wh = 240.0f / menu_zoom_scale();
	}
}


/* Left-handed play: the D-pad and the face buttons swap places
   (Y<->Up, A<->Right, B<->Down, X<->Left). Toggled from the manual, kept as a marker file. */
static int Lefty = -1;   /* -1: not read yet */
int n3ds_lefty(void)
{
	if (Lefty < 0) { struct stat sb; Lefty = (stat(FS2_DIR "/.lefty", &sb) == 0) ? 1 : 0; }
	return Lefty;
}
void n3ds_set_lefty(int on)
{
	Lefty = on ? 1 : 0;
	n3ds_marker_set(FS2_DIR "/.lefty", Lefty);       /* FS2-Q4: the write is checked */
}
static u32 lefty_remap(u32 m)
{
	static const u32 pr[4][2] = { { BTN_Y, BTN_DUP }, { BTN_A, BTN_DRIGHT }, { BTN_B, BTN_DDOWN }, { BTN_X, BTN_DLEFT } };
	if (!n3ds_lefty()) return m;
	u32 o = m;
	for (int i = 0; i < 4; i++) {
		o &= ~(pr[i][0] | pr[i][1]);
		if (m & pr[i][0]) o |= pr[i][1];
		if (m & pr[i][1]) o |= pr[i][0];
	}
	return o;
}

static int held_touch_now(void) { return (!swapped && (hidKeysHeld() & BTN_TOUCH)) ? 1 : 0; }

/* move the menu pointer by (vx, vy) engine pixels, keeping the fraction between frames */
static void move_pointer(float vx, float vy)
{
	static float fx = -1, fy = -1;
	if (fx < 0 || (int)fx != touch_x || (int)fy != touch_y) { fx = (float)touch_x; fy = (float)touch_y; }
	fx += vx;
	fy += vy;
	if (fx < 0) fx = 0;
	if (fy < 0) fy = 0;
	if (fx > N3DS_GAME_W - 1) fx = N3DS_GAME_W - 1;
	if (fy > N3DS_GAME_H - 1) fy = N3DS_GAME_H - 1;
	touch_x = (int)fx;
	touch_y = (int)fy;
	view_follow = 1;
}

/* Menus with the game on the top screen (not swapped): the touchscreen is the dashboard,
   and away from its buttons it is a touchpad for the menu pointer, like a laptop's: sliding
   moves the pointer by how far the stylus moves (not to where it is), a short tap clicks
   where the pointer is (R held: right button). The pointer is drawn on the top screen. */
static int input_frame = 0;
static int pad_click_left = 0, pad_click_right = 0;   /* frames a tap's click is still held */
static int r_modifier_used = 0;                       /* R was held for a right-click tap */

static void menu_touchpad(int touching, int tx, int ty, int r_held)
{
	static int was = 0, lx, ly, moved, start;
	if (touching) {
		if (!was) { lx = tx; ly = ty; moved = 0; start = input_frame; }
		else {
			int dx = tx - lx, dy = ty - ly;
			if (dx || dy) {
				/* a little faster than 1:1 on the top screen, so one slide crosses it */
				float k = 1.5f / menu_zoom_scale();
				move_pointer(dx * k, dy * k);
				moved += abs(dx) + abs(dy);
				lx = tx; ly = ty;
			}
		}
	} else if (was && moved < 8 && input_frame - start < 20 && !pad_click_left && !pad_click_right) {
		/* a tap: the button is held 6 frames so the menu's poll sees it (one click, never queued) */
		if (r_held) { pad_click_right = 6; r_modifier_used = 1; }
		else pad_click_left = 6;
	}
	was = touching;
}

extern "C" void n3ds_menu_zoom(int delta)
{
	if (delta == 0) {               /* ZOOM button: cycle */
		menu_zoom = (menu_zoom + 1) % 3;
		return;
	}
	menu_zoom += delta;
	if (menu_zoom < 0) menu_zoom = 0;
	if (menu_zoom > 2) menu_zoom = 2;
}

extern "C" void n3ds_audio_shutdown(void);

/* quiet click for touch buttons */
extern "C" void n3ds_click(void)
{
	if ((int)Snds_iface.size() > SND_IFACE_MOUSE_CLICK)
		snd_play(&Snds_iface[SND_IFACE_MOUSE_CLICK], 0.0f, 0.15f);
}

extern "C" void n3ds_tap_key(int key)
{
	key_mark(key, 1, 0);
	key_mark(key, 0, 0);
}

/* FS-05: HOME pressed or the lid closed. libctru calls the APT hooks from inside
   aptJumpToHomeMenu() / aptHandleSleep(), after the HOME transition has begun; a save in there
   froze HOME on real hardware in Star Fleet II (Nov 20 2026), so nothing here writes a file,
   logs or waits. The hook only pauses the game's clock (game_stop_time / game_start_time: two
   counters, as the engine's own pause; the engine runs on this same thread, so nothing races)
   and flags the way back. The keys are let go on the main thread: before aptMainLoop handles a
   HOME press (n3ds_apt_before) and after we are back (n3ds_apt_after). The pilot is not saved on
   HOME or sleep itself; HOME -> Close and power off save it in n3ds_input_poll, as START > EXIT
   GAME does. The system keyboard applet suspends us too: then the clock is left alone (the
   keyboard is how a pilot is named). */
void game_stop_time();
void game_start_time();
static int n3ds_suspended = 0;
static volatile int n3ds_resumed = 0;
static aptHookCookie n3ds_apt_cookie;

static void n3ds_apt_hook(APT_HookType h, void *)
{
	if (n3ds_quit || n3ds_in_keyboard) return;
	if ((h == APTHOOK_ONSUSPEND || h == APTHOOK_ONSLEEP) && !n3ds_suspended) {
		n3ds_suspended = 1;
		game_stop_time();
	} else if ((h == APTHOOK_ONRESTORE || h == APTHOOK_ONWAKEUP) && n3ds_suspended) {
		n3ds_suspended = 0;
		game_start_time();
		n3ds_resumed = 1;
	}
}

/* Main thread, just before aptMainLoop: a HOME press is waiting - let go of every key, so none
   is stuck down when we come back. */
static void n3ds_apt_before(void)
{
	if (!n3ds_in_keyboard && aptShouldJumpToHome()) release_all_keys();
}

/* Main thread, aptMainLoop came back true: back from the HOME Menu or sleep. */
static void n3ds_apt_after(void)
{
	if (!n3ds_resumed) return;
	n3ds_resumed = 0;
	release_all_keys();
	n3ds_log("resumed\n");
}

extern "C" void n3ds_input_poll(void)
{
	if (n3ds_quit) return;
	{
		static int hooked = 0;
		if (!hooked) { aptHook(&n3ds_apt_cookie, n3ds_apt_hook, NULL); hooked = 1; }
	}
	if (!n3ds_exit_requested) n3ds_apt_before();
	if (n3ds_exit_requested || !aptMainLoop()) {
		/* HOME -> Close (or the START menu's EXIT GAME -> YES) (or power off): the system waits for us to exit, so do it right
		   away instead of running the engine's long shutdown. All on the engine's main thread, outside the APT hooks: the
		   pilot is saved (one small file, <name>.tmp + rename; only once a pilot exists, the same rule for every way out),
		   then the mixer is stopped (its thread is joined: it only ever waits on its own event, which is signalled first,
		   and on the AL lock, which nothing holds here) and the services shut down; no game thread is waited on (the
		   engine has none). */
		n3ds_quit = 1;
		u64 c0 = osGetTime();
		n3ds_note("close requested - saving pilot and exiting\n");
		if (Player && Player->callsign[0]) {
			Pilot.save_player(Player);
			n3ds_note("close: pilot saved in %llu ms\n", (unsigned long long)(osGetTime() - c0));
		}
		n3ds_threads_stop();   /* FS-01: timer and stall threads before the mixer and the services */
		n3ds_audio_shutdown();
		n3ds_note("bye\n");
		if (n3ds_logfile) { fclose(n3ds_logfile); n3ds_logfile = NULL; }
		pglExit();
		hidExit();
		gfxExit();
		/* _exit, not exit: the engine's atexit handlers would start the slow full
		   shutdown (and draw with freed buffers) - the system only needs us gone */
		_exit(0);
	}
	n3ds_apt_after();

	hidScanInput();
	input_frame++;
	u32 held = lefty_remap(hidKeysHeld());
	u32 down = lefty_remap(hidKeysDown());
	u32 up = lefty_remap(hidKeysUp());
	{	/* rule 16: the swap is remembered (marker file, like .lefty) */
		static int loaded = 0;
		if (!loaded) { struct stat sb; swapped = stat(FS2_DIR "/.swapped", &sb) == 0; loaded = 1; }
	}
	if (down & BTN_ZL) {
		swapped = !swapped;
		n3ds_marker_set(FS2_DIR "/.swapped", swapped);   /* FS2-Q4: the write is checked */
	}
	held &= ~BTN_ZL; down &= ~BTN_ZL; up &= ~BTN_ZL;
	/* swapped: the touchscreen shows the game picture, so the stylus works on that
	   picture (menus: it is the mouse; manual: drag scrolls) instead of the panels */
	u32 raw_touch_held = held & BTN_TOUCH;
	if (swapped) { held &= ~BTN_TOUCH; down &= ~BTN_TOUCH; up &= ~BTN_TOUCH; }
	if (down) {
		touchPosition ltp; hidTouchRead(&ltp);
		n3ds_log("input down %08lx touch %d,%d\n", (unsigned long)down, ltp.px, ltp.py);
	}

	int flight = in_flight();
	static int was_flight = -1;
	static int panel_nav = 0;         /* flight: D-Pad / A work the cockpit panel's buttons */
	static int select_used = 0;
	/* The START menu opened in flight pauses the game, and the engine's pause is a state of its own
	   (GS_STATE_GAME_PAUSED, not GAME_PLAY): that change must not close the menu it was made for
	   (on hardware the menu vanished at once and left the game's own PAUSED screen). */
	static int pm_from_flight = 0;    /* the open START menu was opened in flight */
	static int pm_esc_pending = 0;    /* GAME MENU: Esc goes to the game once it is flying again */
	const int paused = gameseq_get_state() == GS_STATE_GAME_PAUSED;
	if (flight != was_flight) {
		release_all_keys();
		if (n3ds_portmenu_is_open() && !(pm_from_flight && paused)) n3ds_portmenu_close();   /* the game moved on (a mission ended) under an open menu */
		was_flight = flight;
		panel_nav = 0;
		n3ds_panel_set_nav(0);
	}

	/* START port menu (house style), in menus and in flight (FS-08: one menu): START alone opens it
	   (SELECT + START is still the manual); while it is open it takes every button and the touch,
	   and nothing reaches the game. In flight the game is paused while it is open (the same pause
	   key the manual uses) and it has a GAME MENU row: the game's own pause / options / abort screen,
	   which START used to send Esc for. B is still the game's back key in menus. */
	static int pm_was_open = 0;
	if (pm_esc_pending > 0) {         /* GAME MENU was chosen: the game's own pause / abort screen */
		if (flight) { pm_esc_pending = 0; key_mark(KEY_ESC, 1, 0); key_mark(KEY_ESC, 0, 0); }
		else pm_esc_pending--;
	}
	if (!n3ds_manual_is_open()) {
		if (n3ds_portmenu_is_open()) {
			touchPosition ptp; hidTouchRead(&ptp);
			int th = (held & BTN_TOUCH) ? 1 : 0, td = (down & BTN_TOUCH) ? 1 : 0;
			int dy = (down & BTN_DDOWN) ? 1 : (down & BTN_DUP) ? -1 : 0;
			if (!dy) dy = (down & BTN_DRIGHT) ? 1 : (down & BTN_DLEFT) ? -1 : 0;
			n3ds_portmenu_input(dy, (down & BTN_A) ? 1 : 0, (down & BTN_B) ? 1 : 0, (down & BTN_START) ? 1 : 0,
				th, th || td ? ptp.px : 0, th || td ? ptp.py : 0, td);
			release_all_keys();
			if (touch_down) { mouse_mark_button(MOUSE_LEFT_BUTTON, 0); touch_down = 0; }
			if (!n3ds_portmenu_is_open() && pm_was_open) {
				pm_was_open = 0;
				/* closed: in flight the game was paused for the menu. RESUME, B and GAME MENU unpause;
				   MANUAL keeps it paused (closing the manual unpauses) and EXIT is leaving anyway */
				if (pm_from_flight && paused && !n3ds_manual_is_open() && !n3ds_exit_requested) { key_mark(KEY_PAUSE, 1, 0); key_mark(KEY_PAUSE, 0, 0); }
				if (n3ds_pm_game_menu_requested) {
					n3ds_pm_game_menu_requested = 0;
					if (pm_from_flight) pm_esc_pending = 120;   /* sent below, once the game has left its pause state */
				}
				pm_from_flight = 0;
			}
			return;
		}
		/* FS2-U1: a pop-up with answers is up: its message page (big buttons on the dashboard's or
		   the cockpit panel's screen) takes the D-Pad, A, B and the touch; nothing else reaches the
		   game until it is answered. START does nothing here (the pop-up has to be answered first). */
		{
			touchPosition qtp; hidTouchRead(&qtp);
			int th = (held & BTN_TOUCH) ? 1 : 0, td = (down & BTN_TOUCH) ? 1 : 0;
			int dy = (down & BTN_DDOWN) ? 1 : (down & BTN_DUP) ? -1 : 0;
			if (!dy) dy = (down & BTN_DRIGHT) ? 1 : (down & BTN_DLEFT) ? -1 : 0;
			/* in menus X is the game's Enter ("accept"): there it presses the highlighted answer too */
			if (n3ds_popup_page_input(flight, dy, (down & (flight ? BTN_A : (BTN_A | BTN_X))) ? 1 : 0, (down & BTN_B) ? 1 : 0,
					th, th || td ? qtp.px : 0, th || td ? qtp.py : 0, td)) {
				release_all_keys();
				if (touch_down) { mouse_mark_button(MOUSE_LEFT_BUTTON, 0); touch_down = 0; }
				pm_esc_pending = 0;
				return;
			}
		}
		if ((down & BTN_START) && !(held & BTN_SELECT)) {
			n3ds_click();
			n3ds_portmenu_open(flight);
			pm_was_open = 1;
			pm_from_flight = flight;
			pm_esc_pending = 0;
			release_all_keys();
			if (flight) { key_mark(KEY_PAUSE, 1, 0); key_mark(KEY_PAUSE, 0, 0); }
			if (touch_down) { mouse_mark_button(MOUSE_LEFT_BUTTON, 0); touch_down = 0; }
			return;
		}
	}

	/* Everything you can tap also works with the buttons, for when the panel or dashboard is
	   on the top screen (ZL) and can't be touched: the D-Pad moves a gold highlight between
	   its buttons and A presses the highlighted one. */
	const u32 BTN_DPAD = BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT;
	int nav_dx = (down & BTN_DRIGHT) ? 1 : (down & BTN_DLEFT) ? -1 : 0;
	int nav_dy = (down & BTN_DDOWN) ? 1 : (down & BTN_DUP) ? -1 : 0;
	if (flight && !n3ds_manual_is_open()) {
		/* cockpit panel: SELECT + B turns its highlight on / off (B alone also turns it off);
		   while it is on, the D-Pad and A work the panel instead of their flight functions */
		if ((held & BTN_SELECT) && (down & BTN_B)) {
			panel_nav = !panel_nav;
			select_used = 1;
			down &= ~BTN_B; held &= ~BTN_B;
			n3ds_panel_set_nav(panel_nav);
		} else if (panel_nav && (down & BTN_B)) {
			panel_nav = 0;
			down &= ~BTN_B; held &= ~BTN_B;
			n3ds_panel_set_nav(0);
		}
		if (panel_nav) {
			n3ds_panel_nav(nav_dx, nav_dy, (down & BTN_A) ? 1 : 0);
			down &= ~(BTN_DPAD | BTN_A); held &= ~(BTN_DPAD | BTN_A);
		} else {
			n3ds_panel_nav(0, 0, 0);       /* finishes a press in progress */
		}
	} else if (flight) {
		n3ds_panel_nav(0, 0, 0);
	}
	/* menus with the screens swapped: the dashboard is on top; the stylus is the mouse below */
	n3ds_dash_set_nav(swapped);
	if (!flight) {
		if (swapped && !n3ds_manual_is_open()) {
			n3ds_dash_nav(nav_dx, nav_dy, (down & BTN_A) ? 1 : 0);
			down &= ~(BTN_DPAD | BTN_A); held &= ~(BTN_DPAD | BTN_A);
		} else {
			n3ds_dash_nav(0, 0, 0);
		}
	}

	/* panel buttons take the touch while flying */
	touchPosition tp;
	hidTouchRead(&tp);
	if (flight && !n3ds_manual_is_open()) {
		n3ds_panel_touch((held & BTN_TOUCH) ? 1 : 0, tp.px, tp.py, (down & BTN_TOUCH) ? 1 : 0);
		touch_down = 0;
	} else {
		/* menus are on the top screen; the touchscreen is the dashboard with buttons */
		if (!n3ds_manual_is_open()) {
			/* a touch that starts on a dashboard button is that button's; anywhere else on
			   the dashboard it is the touchpad */
			static int touch_owner = 0;        /* 1 dashboard button, 2 touchpad */
			int th = (held & BTN_TOUCH) ? 1 : 0, td = (down & BTN_TOUCH) ? 1 : 0;
			if (td) touch_owner = (n3ds_dash_button_at(tp.px, tp.py) >= 0) ? 1 : 2;
			n3ds_dash_touch(th && touch_owner == 1, tp.px, tp.py, td && touch_owner == 1);
			menu_touchpad(th && touch_owner == 2, tp.px, tp.py, (held & BTN_R) ? 1 : 0);
			if (!th) touch_owner = 0;
		}

		/* Circle Pad and C-Stick move the pointer: slow near the centre for precision,
		   fast at the rim. The top-screen view follows the pointer. */
		if (!n3ds_manual_is_open()) {
			circlePosition cp, cs;
			hidCircleRead(&cp);
			hidCstickRead(&cs);
			float vx = 0, vy = 0;
			const circlePosition *sticks[2] = { &cp, &cs };
			const float range[2] = { 150.0f, 140.0f };
			for (int si = 0; si < 2; si++) {
				int dx = sticks[si]->dx, dy = sticks[si]->dy;
				int mag = dx * dx + dy * dy;
				if (mag <= 18 * 18) continue;
				float m = sqrtf((float)mag);
				float t = (m - 18.0f) / (range[si] - 18.0f);
				if (t > 1.0f) t = 1.0f;
				float speed = (2.0f + 12.0f * t * sqrtf(t)) / menu_zoom_scale();   /* screen pixels per frame */
				vx += dx / m * speed;
				vy -= dy / m * speed;
			}
			if (vx != 0 || vy != 0) move_pointer(vx, vy);
			/* L / R zoom the top-screen view. R zooms when it is let go, unless it was held
			   for a right-click tap on the touchpad. */
			if (down & BTN_L) n3ds_menu_zoom(-1);
			if (down & BTN_R) r_modifier_used = 0;
			if (up & BTN_R) {
				if (!r_modifier_used) n3ds_menu_zoom(+1);
				r_modifier_used = 0;
			}
		}
		/* swapped: the stylus on the menu picture (the whole 320x240 touchscreen) is the
		   mouse. The button goes down one frame after the pointer moves there, so the
		   menu has seen the pointer over the button before the click lands. */
		static int stylus_frames = 0;
		int stylus = 0;
		if (swapped && !n3ds_manual_is_open() && raw_touch_held) {
			/* the stylus points into the view shown on the touchscreen (the whole frame at zoom
			   level 0, a part of it when zoomed in: FS2-U2) */
			float vw, vh;
			menu_view_size(&vw, &vh);
			touch_x = (int)(view_x + tp.px * vw / 320.0f);
			touch_y = (int)(view_y + tp.py * vh / 240.0f);
			if (touch_x > N3DS_GAME_W - 1) touch_x = N3DS_GAME_W - 1;
			if (touch_y > N3DS_GAME_H - 1) touch_y = N3DS_GAME_H - 1;
			if (touch_x < 0) touch_x = 0;
			if (touch_y < 0) touch_y = 0;
			stylus = (++stylus_frames >= 2);
			view_follow = 0;
		} else {
			stylus_frames = 0;
		}
		/* A (or the stylus, or a touchpad tap) = left mouse button; a tap with R held = right */
		if (pad_click_left) pad_click_left--;
		int now = ((held & BTN_A) || stylus || pad_click_left) ? 1 : 0;
		if (now != touch_down) {
			/* make sure the cursor position is updated before the click lands */
			mouse_mark_button(MOUSE_LEFT_BUTTON, now);
			touch_down = now;
		}
		static int right_down = 0;
		if (pad_click_right) pad_click_right--;
		int rnow = pad_click_right ? 1 : 0;
		if (rnow != right_down) {
			mouse_mark_button(MOUSE_RIGHT_BUTTON, rnow);
			right_down = rnow;
		}
	}

	/* SELECT + START: built-in manual (pauses the game in flight) */
	if ((held & BTN_SELECT) && (down & BTN_START)) {
		n3ds_manual_toggle();
		release_all_keys();
		if (flight) { key_mark(KEY_PAUSE, 1, 0); key_mark(KEY_PAUSE, 0, 0); }
		return;
	}
	if (n3ds_manual_is_open()) {
		/* the standard manual controls of all the 3DS ports */
		circlePosition mcp, mcs;
		hidCircleRead(&mcp);
		hidCstickRead(&mcs);
		n3ds_manual_scroll(abs(mcp.dy) >= abs(mcs.dy) ? mcp.dy : mcs.dy);
		if (down & BTN_DLEFT)  n3ds_manual_dpad(-1, 0);
		if (down & BTN_DRIGHT) n3ds_manual_dpad(1, 0);
		if (down & BTN_DUP)    n3ds_manual_dpad(0, -1);
		if (down & BTN_DDOWN)  n3ds_manual_dpad(0, 1);
		if (down & BTN_A)      n3ds_manual_select();
		if (down & BTN_R)      n3ds_manual_page(1);
		if (down & BTN_L)      n3ds_manual_page(-1);
		if (swapped) {
			/* the text is on the touchscreen: drag it to scroll */
			static int last_ty = -1;
			if (raw_touch_held) {
				if (last_ty >= 0) n3ds_manual_scroll_by((last_ty - tp.py) * 240.0f / 192.0f);
				last_ty = tp.py;
			} else {
				last_ty = -1;
			}
		} else {
			n3ds_manual_touch((held & BTN_TOUCH) ? 1 : 0, tp.px, tp.py, (down & BTN_TOUCH) ? 1 : 0);
		}
		if (n3ds_manual_take_close() || (down & (BTN_B | BTN_START))) {   /* the CLOSE button does what B does */
			n3ds_manual_close();
			if (flight) { key_mark(KEY_PAUSE, 1, 0); key_mark(KEY_PAUSE, 0, 0); }
		}
		return;
	}

	/* buttons */
	int select_held = (held & BTN_SELECT) && flight;
	for (unsigned i = 0; i < sizeof(n3ds_keys) / sizeof(n3ds_keys[0]); i++) {
		const keymap &k = n3ds_keys[i];
		if (k.button == BTN_SELECT && flight) continue;   /* handled below (tap = comms) */
		int key = flight ? k.flight_key : k.menu_key;
		if (select_held) {
			for (unsigned j = 0; j < sizeof(n3ds_select_keys) / sizeof(n3ds_select_keys[0]); j++) {
				if (n3ds_select_keys[j].button == k.button) {
					key = n3ds_select_keys[j].flight_key;
					if (down & k.button) select_used = 1;
				}
			}
		}
		if (down & k.button) send_key(key, 1);
		if (up & k.button) {
			send_key(flight ? k.flight_key : k.menu_key, 0);
			for (unsigned j = 0; j < sizeof(n3ds_select_keys) / sizeof(n3ds_select_keys[0]); j++)
				if (n3ds_select_keys[j].button == k.button) send_key(n3ds_select_keys[j].flight_key, 0);
		}
	}
	/* SELECT on its own (tap) opens the comms menu */
	if (flight) {
		if (down & BTN_SELECT) select_used = 0;
		if ((up & BTN_SELECT) && !select_used) {
			key_mark(KEY_C, 1, 0);
			key_mark(KEY_C, 0, 0);
		}
	}

	/* sticks */
	circlePosition cp, cs;
	hidCircleRead(&cp);
	hidCstickRead(&cs);
	/* small dead zone of our own: a worn Circle Pad rests a few counts off centre */
	if (cp.dx > -12 && cp.dx < 12 && cp.dy > -12 && cp.dy < 12) { cp.dx = 0; cp.dy = 0; }
	if (cs.dx > -12 && cs.dx < 12) cs.dx = 0;
	if (cs.dy > -12 && cs.dy < 12) cs.dy = 0;
	joy_axes[0] = scale_axis(cp.dx, 150);
	joy_axes[1] = -scale_axis(cp.dy, 150);
	joy_axes[3] = scale_axis(cs.dx, 140);   /* bank */
	joy_axes[4] = -scale_axis(cs.dy, 140);

	/* never fly with the "mouse": new pilots default to mouse flight, and the stylus /
	   pointer position would keep turning the ship with nobody touching anything */
	extern int Use_mouse_to_fly;
	Use_mouse_to_fly = 0;

	joy_axes[2] = 0;

	joy_hat = 0;
	memset(joy_buttons, 0, sizeof(joy_buttons));
}

/* ------------------------------------------------------------------ perf test flags */
/* /3ds/freespace2/perf_flags holds words such as "no3d nopanel": test runs switch parts of
   the frame off to find where the GPU time goes (Nov 16). Read once at start. */
static char perf_flags[128] = "";
static int perf_flags_read = 0;
extern "C" int n3ds_perf_flag(const char *name)
{
	if (!perf_flags_read) {
		perf_flags_read = 1;
		FILE *f = fopen("sdmc:/3ds/freespace2/perf_flags", "r");
		if (f) { size_t n = fread(perf_flags, 1, sizeof(perf_flags) - 1, f); perf_flags[n] = 0; fclose(f); }
		n3ds_log("perf flags: '%s'\n", perf_flags);
	}
	return perf_flags[0] && strstr(perf_flags, name) != NULL;
}

/* "key=a-b" in perf_flags: 1 and the range if present (test bisection) */
extern "C" int n3ds_perf_range(const char *key, int *a, int *b)
{
	n3ds_perf_flag("");   /* reads the file once */
	char pat[24]; snprintf(pat, sizeof pat, "%s=", key);
	const char *p = strstr(perf_flags, pat);
	if (!p) return 0;
	return sscanf(p + strlen(pat), "%d-%d", a, b) == 2;
}

/* ------------------------------------------------------------------ profiler */

static inline float ticks_ms_early(u64 t) { return (float)((double)t / (SYSCLOCK_ARM11 / 1000.0)); }

static u64 prof_start[N3DS_PROF_COUNT];
static u64 prof_total[N3DS_PROF_COUNT];

/* GPU probe: on one frame in 300 the CPU waits for the GPU at each stage boundary, so
   each wait measures how long the GPU took for that stage alone (logged as "gpu probe") */
static int probe_frame = 0;
static float probe_ms[5];
static float gpu_finish_ms(void)
{
	u64 t = svcGetSystemTick();
	glFinish();
	return ticks_ms_early(svcGetSystemTick() - t);
}

extern "C" void n3ds_prof_begin(int id)
{
	if (probe_frame) {
		if (id == N3DS_PROF_SIM) gpu_finish_ms();                 /* drain the previous frame */
		if (id == N3DS_PROF_3D)  probe_ms[0] = gpu_finish_ms();   /* clear + anything before 3D */
	}
	prof_start[id] = svcGetSystemTick();
}
extern "C" void n3ds_prof_end(int id)
{
	if (probe_frame) {
		if (id == N3DS_PROF_3D)  probe_ms[1] = gpu_finish_ms();
		if (id == N3DS_PROF_HUD) probe_ms[2] = gpu_finish_ms();
	}
	prof_total[id] += svcGetSystemTick() - prof_start[id];
}

static inline float ticks_ms(u64 t) { return (float)((double)t / (SYSCLOCK_ARM11 / 1000.0)); }

/* every 300 frames: average ms per frame for each stage, GPU waits and draw counts */
static void prof_report(int frames_in_window, u64 window_ticks, int state)
{
	pglFrameStats gs;
	pglGetStats(&gs, 1);
	float n = (float)frames_in_window;
	float ms = ticks_ms(window_ticks);
	n3ds_log("perf state %d: %.1f fps | ms/frame sim %.1f 3d %.1f hud %.1f panel %.1f present %.1f | gpu-wait %.1f swap-wait %.1f | per frame: %u polys -> %u draws, %u arrays, %u flushes, %u verts | linear free %d KB | swap watchdog %u\n",
		state, n * 1000.0f / ms,
		ticks_ms(prof_total[N3DS_PROF_SIM]) / n, ticks_ms(prof_total[N3DS_PROF_3D]) / n,
		ticks_ms(prof_total[N3DS_PROF_HUD]) / n, ticks_ms(prof_total[N3DS_PROF_PANEL]) / n,
		ticks_ms(prof_total[N3DS_PROF_PRESENT]) / n,
		gs.flushWaitMs / n, gs.swapWaitMs / n,
		(unsigned)(gs.ends / n), (unsigned)(gs.batches / n), (unsigned)(gs.arrayDraws / n),
		(unsigned)(gs.flushes / n), (unsigned)(gs.verts / n), (int)(linearSpaceFree() / 1024), gs.swapTimeouts);
	{
		extern unsigned pgl_tex_uploads, pgl_tex_upload_px, pgl_tex_linear_binds;
		n3ds_log("tex per frame: %u uploads, %u px, %u binds of textures in linear memory\n",
			(unsigned)(pgl_tex_uploads / n), (unsigned)(pgl_tex_upload_px / n), (unsigned)(pgl_tex_linear_binds / n));
		pgl_tex_uploads = pgl_tex_upload_px = pgl_tex_linear_binds = 0;
	}
	memset(prof_total, 0, sizeof(prof_total));
}

/* ------------------------------------------------------------------ screens */

/*
 * Draw a rectangle of the main render target (engine coords, top-left origin) onto the
 * currently bound screen target rectangle (screen coords, top-left origin).
 */
#ifndef N3DS_RT_FLIP_T
#define N3DS_RT_FLIP_T 0
#endif

static int top_wide = 0;            /* this frame's top screen is drawn 800x240 (3D slider down) */
static float compose_xs = 1.0f;     /* target pixels per layout pixel across (2 on the wide top) */

static void main_uv(float ex, float ey, float *s, float *t)
{
	int w, h, gw, gh;
	pglMainSize(&w, &h, &gw, &gh);
	/* engine pixel (ex, ey) with top-left origin -> GL landscape (lx, ly) bottom-left origin */
	float lx = ex;
	float ly = (float)h - ey;
	/* picaGL stores landscape x along GPU y (reversed) and landscape y along GPU x */
	float gx = ly;
	float gy = (float)w - lx;
	*s = gx / (float)gw;
	*t = N3DS_RT_FLIP_T ? 1.0f - gy / (float)gh : gy / (float)gh;
}

static void blit_main(float sx0, float sy0, float sx1, float sy1, float ex0, float ey0, float ex1, float ey1)
{
	int tw, th;
	pglGetTargetSize(&tw, &th);

	/* whole-number scales (1:1, or 2x across on the wide top) are copied with nearest
	   sampling, so text stays exactly as drawn; anything else is filtered */
	float scx = (sx1 - sx0) * compose_xs / (ex1 - ex0), scy = (sy1 - sy0) / (ey1 - ey0);
	int whole = scx >= 0.999f && scy >= 0.999f && fabsf(scx - roundf(scx)) < 0.001f && fabsf(scy - roundf(scy)) < 0.001f;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, whole ? GL_NEAREST : GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, whole ? GL_NEAREST : GL_LINEAR);

	float s[4], t[4];
	main_uv(ex0, ey0, &s[0], &t[0]);   /* top-left */
	main_uv(ex1, ey0, &s[1], &t[1]);   /* top-right */
	main_uv(ex1, ey1, &s[2], &t[2]);   /* bottom-right */
	main_uv(ex0, ey1, &s[3], &t[3]);   /* bottom-left */

	glBegin(GL_QUADS);
	glTexCoord2f(s[0], t[0]); glVertex3f(sx0, (float)th - sy0, 0.0f);
	glTexCoord2f(s[1], t[1]); glVertex3f(sx1, (float)th - sy0, 0.0f);
	glTexCoord2f(s[2], t[2]); glVertex3f(sx1, (float)th - sy1, 0.0f);
	glTexCoord2f(s[3], t[3]); glVertex3f(sx0, (float)th - sy1, 0.0f);
	glEnd();
}

static GLuint compose_src_tex = 0;

static void compose_begin(int target)
{
	pglBindTarget(target);
	int tw, th;
	pglGetTargetSize(&tw, &th);
	/* the wide top keeps the 400x240 layout coordinates, scaled 2x across */
	compose_xs = (target == PGL_TARGET_TOP && top_wide) ? 2.0f : 1.0f;

	glViewport(0, 0, tw, th);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_STENCIL_TEST);
	glDepthMask(GL_FALSE);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0, tw / compose_xs, 0, th, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);

	glActiveTexture(GL_TEXTURE1);
	glDisable(GL_TEXTURE_2D);
	glActiveTexture(GL_TEXTURE0);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, compose_src_tex ? compose_src_tex : n3ds_main_tex);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glColor4f(1, 1, 1, 1);
}

static void compose_end(void)
{
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
}

/* draw an overlay with the engine into the overlay target (same coordinates as the
   main target, so the engine's 2D code works unchanged) */
static void overlay_begin(void)
{
	pglBindTarget(PGL_TARGET_OVERLAY);
	pglRestoreViewport();
	opengl_state_n3ds_resync();
}

/* copy the overlay's top-left w x 240 1:1 to a screen */
static void overlay_to(int target, int w)
{
	compose_src_tex = n3ds_overlay_tex;
	compose_begin(target);
	blit_main(0, 0, (float)w, 240, 0, 0, (float)w, 240);
	compose_end();
	compose_src_tex = 0;
}

/* like overlay_to, but into a destination rectangle of the screen (scaled / centred) */
static void overlay_to_ex(int target, int w, float dx0, float dy0, float dx1, float dy1)
{
	compose_src_tex = n3ds_overlay_tex;
	compose_begin(target);
	blit_main(dx0, dy0, dx1, dy1, 0, 0, (float)w, 240);
	compose_end();
	compose_src_tex = 0;
}

/* ---- engine save_screen / restore_screen (popups, pause): GPU copy of the frame */
static int snap_valid = 0;
static float snap_scale = 1.0f;

extern "C" int n3ds_save_screen(void)
{
	pglSnapshotMain(n3ds_snap_tex);
	snap_scale = pglGetRenderScale();
	snap_valid = 1;
	opengl_state_n3ds_resync();
	return 1;
}

extern "C" void n3ds_free_screen(void) { snap_valid = 0; }

extern "C" void n3ds_restore_screen(void)
{
	if (!snap_valid) return;
	pglSaveViewport();
	glViewport(0, 0, N3DS_GAME_W, N3DS_GAME_H);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_STENCIL_TEST);
	glDepthMask(GL_FALSE);
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0, N3DS_GAME_W, 0, N3DS_GAME_H, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	glActiveTexture(GL_TEXTURE1);
	glDisable(GL_TEXTURE_2D);
	glActiveTexture(GL_TEXTURE0);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, n3ds_snap_tex);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glColor4f(1, 1, 1, 1);

	float sy0 = 0, sx1 = N3DS_GAME_W;
	if (snap_scale < 0.99f) { sy0 = N3DS_GAME_H - N3DS_GAME_H * snap_scale; sx1 = N3DS_GAME_W * snap_scale; }
	blit_main(0, 0, N3DS_GAME_W, N3DS_GAME_H, 0, sy0, sx1, N3DS_GAME_H);

	compose_end();
	pglRestoreViewport();
	opengl_state_n3ds_resync();
}

/* Stall trace (Nov 18 2026, SM1-01 hangs ~2 s into flight): the game marks where it is
   (N3DS_FT in freespace.cpp sets n3ds_phase / n3ds_phase_line every frame step); if frames
   stop for 3 s, this thread writes the last mark to fs2_3ds.log, then again every 10 s. */
extern "C" { volatile const char* n3ds_phase = "start"; volatile int n3ds_phase_line = 0; }
static volatile u32 n3ds_present_count = 0;
static void stall_thread(void*)
{
	u32 last = 0; u64 still = osGetTime(), lastLog = 0;
	while (n3ds_stall_run) {
		for (int i = 0; i < 5 && n3ds_stall_run; i++) svcSleepThread(100000000LL);   /* 0.5 s, stoppable */
		if (!n3ds_stall_run) break;
		const u64 now = osGetTime();
		const u32 c = n3ds_present_count;
		if (c != last) { last = c; still = now; continue; }
		if (c == 0 || now - still < 3000) continue;
		if (lastLog && now - lastLog < 10000) continue;
		lastLog = now;
		n3ds_note("STALL: no frame for %.1f s after frame %lu; last step '%s' (freespace.cpp line %d)\n",
		         (double)(now - still) / 1000.0, (unsigned long)c, (const char*)n3ds_phase, (int)n3ds_phase_line);
	}
}

extern "C" void n3ds_present(void)
{
	int flight = in_flight();

	static int frames = 0, last_state = -1;
	int state = gameseq_get_state();
	if (state != last_state || frames == 0) {
		n3ds_log("frame %d: game state %d\n", frames, state);
		last_state = state;
	}
	frames++;
	n3ds_present_count++;
	{ static bool st = false; if (!st) { st = true; s32 pr = 0x30; svcGetThreadPriority(&pr, CUR_THREAD_HANDLE);
	  n3ds_stall_th = threadCreate(stall_thread, NULL, 8 * 1024, pr - 2, -2, false);   /* joined at exit (FS-01) */
	  if (!n3ds_stall_th) n3ds_note("stall trace: no thread\n"); } }
	static u64 window_t0 = 0;
	if (!window_t0) window_t0 = svcGetSystemTick();
	if ((frames % 300) == 0) {
		u64 now = svcGetSystemTick();
		n3ds_phase = "present: prof_report"; n3ds_phase_line = 946;
		prof_report(300, now - window_t0, state);
		window_t0 = now;
	}
	n3ds_phase = "present: n3ds_prof_begin"; n3ds_phase_line = 949;
	n3ds_prof_begin(N3DS_PROF_PRESENT);

	/* Top-screen text is sharpest in the LCD's 800x240 wide mode (its own columns; the
	   normal mode doubles each pixel). The game draws no stereo picture, so with the 3D
	   slider down the top is drawn 800 wide (same 400x240 layout, 2x across); with the
	   slider up it goes back to the normal 3D-capable mode. */
	top_wide = pglSetTopWide(osGet3DSliderState() <= 0.0f);

	n3ds_phase = "present: pglSaveViewport"; n3ds_phase_line = 957;
	pglSaveViewport();

	/* where this frame was rendered: at render scale 0.5 the whole 800x480 engine frame
	   sits in the 400x240 GL-bottom-left corner = engine rect (0,240)-(400,480) */
	float fscale = pglGetRenderScale();
	float src_x0 = 0, src_y0 = 0, src_x1 = N3DS_GAME_W, src_y1 = N3DS_GAME_H;
	if (fscale < 0.99f) {
		src_y0 = N3DS_GAME_H - N3DS_GAME_H * fscale;
		src_x1 = N3DS_GAME_W * fscale;
	}
	/* overlays (panel, manual, dashboard) are drawn 1:1 into the engine's top-left corner */
	n3ds_phase = "present: pglSetRenderScale"; n3ds_phase_line = 968;
	pglSetRenderScale(1.0f);

	int bottom = 1;
	/* swapped: the picture is drawn to the bottom every frame, so always present it */
	if (n3ds_manual_is_open()) {
		/* manual: text on top, section buttons on the touchscreen */
		n3ds_phase = "present: overlay_begin"; n3ds_phase_line = 974;
		overlay_begin();
		n3ds_phase = "present: n3ds_manual_draw_text"; n3ds_phase_line = 975;
		n3ds_manual_draw_text();
		if (swapped) overlay_to_ex(PGL_TARGET_BOTTOM, 400, 0, 24, 320, 216);
		else overlay_to(PGL_TARGET_TOP, 400);
		n3ds_phase = "present: overlay_begin"; n3ds_phase_line = 978;
		overlay_begin();
		n3ds_phase = "present: n3ds_manual_draw"; n3ds_phase_line = 979;
		n3ds_manual_draw();
		if (swapped) overlay_to_ex(PGL_TARGET_TOP, 320, 40, 0, 360, 240);
		else overlay_to(PGL_TARGET_BOTTOM, 320);
	} else if (flight) {
		/* ---- top: the cockpit view (rendered at the LCD's own 400x240) */
		if (swapped) {
			n3ds_phase = "present: compose_begin"; n3ds_phase_line = 985;
			compose_begin(PGL_TARGET_BOTTOM);
			n3ds_phase = "present: blit_main"; n3ds_phase_line = 986;
			blit_main(0, 0, 320, 240, src_x0, src_y0, src_x1, src_y1);   /* fills the touchscreen, no bars */
		} else {
			n3ds_phase = "present: compose_begin"; n3ds_phase_line = 988;
			compose_begin(PGL_TARGET_TOP);
			n3ds_phase = "present: blit_main"; n3ds_phase_line = 989;
			blit_main(0, 0, 400, 240, src_x0, src_y0, src_x1, src_y1);
		}
		n3ds_phase = "present: compose_end"; n3ds_phase_line = 991;
		compose_end();

		/* ---- bottom: the manual, or the cockpit panel (drawn by the engine into the
		   corner of the main target, copied 1:1) at 30 Hz - it's only gauges and text */
		if (n3ds_manual_is_open()) {
			n3ds_phase = "present: overlay_begin"; n3ds_phase_line = 996;
			overlay_begin();
			n3ds_phase = "present: n3ds_manual_draw"; n3ds_phase_line = 997;
			n3ds_manual_draw();
			n3ds_phase = "present: overlay_to"; n3ds_phase_line = 998;
			overlay_to(PGL_TARGET_BOTTOM, 320);
		} else if (!n3ds_portmenu_is_open() && n3ds_popup_page_active()) {
			/* FS2-U1: the game asks something in a pop-up: its answers as big buttons, on the panel's screen */
			overlay_begin();
			n3ds_popup_draw_flight();
			if (swapped) overlay_to_ex(PGL_TARGET_TOP, 320, 40, 0, 360, 240);
			else overlay_to(PGL_TARGET_BOTTOM, 320);
		} else if (n3ds_portmenu_is_open()) {
			/* FS-08: the START menu, on the panel's screen, every frame (its buttons animate) */
			overlay_begin();
			n3ds_portmenu_draw_flight();
			if (swapped) overlay_to_ex(PGL_TARGET_TOP, 320, 40, 0, 360, 240);
			else overlay_to(PGL_TARGET_BOTTOM, 320);
		} else if (swapped || (frames & 1)) {
			n3ds_phase = "present: n3ds_prof_begin"; n3ds_phase_line = 1000;
			n3ds_prof_begin(N3DS_PROF_PANEL);
			n3ds_phase = "present: overlay_begin"; n3ds_phase_line = 1001;
			overlay_begin();
			n3ds_phase = "present: n3ds_panel_draw"; n3ds_phase_line = 1002;
			if (!n3ds_perf_flag("nopanel")) n3ds_panel_draw();
			if (swapped) overlay_to_ex(PGL_TARGET_TOP, 320, 40, 0, 360, 240);
			else overlay_to(PGL_TARGET_BOTTOM, 320);
			n3ds_phase = "present: n3ds_prof_end"; n3ds_phase_line = 1005;
			n3ds_prof_end(N3DS_PROF_PANEL);
		} else {
			bottom = 0;
		}
	} else {
		/* ---- top: the menu, at a readable zoom; the view follows the pointer */
		float z = menu_zoom_scale();
		float ww, wh;                                      /* engine pixels in view */
		menu_view_size(&ww, &wh);
		/* a popup opening: centre the view (and the pointer) on it - popups sit in the
		   middle of the screen and are wider than the 1:1 view */
		static int was_popup = 0;
		int popup = popup_active();
		if (popup && !was_popup) {
			view_x = (N3DS_GAME_W - ww) * 0.5f;
			view_y = (N3DS_GAME_H - wh) * 0.5f;
			touch_x = N3DS_GAME_W / 2;
			touch_y = N3DS_GAME_H / 2;
		}
		was_popup = popup;
		const float margin = 48.0f / z;
		if (!swapped || view_follow) {     /* the view never moves because of the stylus (FS2-U2) */
			if (touch_x < view_x + margin) view_x = touch_x - margin;
			if (touch_x > view_x + ww - margin) view_x = touch_x - ww + margin;
			if (touch_y < view_y + margin) view_y = touch_y - margin;
			if (touch_y > view_y + wh - margin) view_y = touch_y - wh + margin;
		}
		if (view_x > N3DS_GAME_W - ww) view_x = N3DS_GAME_W - ww;
		if (view_y > N3DS_GAME_H - wh) view_y = N3DS_GAME_H - wh;
		if (view_x < 0) view_x = 0;
		if (view_y < 0) view_y = 0;
		/* whole engine pixels, so text lands on whole screen pixels */
		view_x = floorf(view_x);
		view_y = floorf(view_y);
		/* map the view (engine coords) into wherever this frame was rendered */
		float kx = (src_x1 - src_x0) / N3DS_GAME_W, ky = (src_y1 - src_y0) / N3DS_GAME_H;
		if (swapped) {
			/* the whole touchscreen, so the stylus can reach everything and there are no bars */
			n3ds_phase = "present: compose_begin"; n3ds_phase_line = 1040;
			compose_begin(PGL_TARGET_BOTTOM);
			n3ds_phase = "present: blit_main"; n3ds_phase_line = 1041;
			/* fills the touchscreen, no bars: the whole frame at zoom level 0, else the view (FS2-U2) */
			blit_main(0, 0, 320, 240, src_x0 + view_x * kx, src_y0 + view_y * ky,
				src_x0 + (view_x + ww) * kx, src_y0 + (view_y + wh) * ky);
		} else {
			n3ds_phase = "present: compose_begin"; n3ds_phase_line = 1043;
			compose_begin(PGL_TARGET_TOP);
			blit_main(0, 0, 400, 240, src_x0 + view_x * kx, src_y0 + view_y * ky,
				src_x0 + (view_x + ww) * kx, src_y0 + (view_y + wh) * ky);
		}
		n3ds_phase = "present: compose_end"; n3ds_phase_line = 1047;
		compose_end();

		/* ---- bottom: the dashboard - nothing that is on the top screen. It only changes
		   when something happens, so it is redrawn at 20 Hz (every frame while touched). */
		static int dash_tick = 0;
		if ((held_touch_now() || swapped || (dash_tick++ % 3) == 0)) {
			n3ds_phase = "present: overlay_begin"; n3ds_phase_line = 1053;
			overlay_begin();
			n3ds_phase = "present: n3ds_dash_draw"; n3ds_phase_line = 1054;
			n3ds_dash_set_nav(swapped);        /* also its width: 400 on the top screen, 320 below */
			if (!n3ds_perf_flag("nodash")) n3ds_dash_draw();
			if (swapped) overlay_to(PGL_TARGET_TOP, 400);   /* FS2-U2: the whole top screen, no side bars */
			else overlay_to(PGL_TARGET_BOTTOM, 320);
		} else {
			bottom = 0;
		}
	}

	if (probe_frame) {
		probe_ms[3] = gpu_finish_ms();     /* top/bottom compose + panel / dashboard */
		n3ds_log("gpu probe (state %d): clear %.1f  3d %.1f  hud %.1f  screens %.1f ms | vram free %d KB\n",
			state, probe_ms[0], probe_ms[1], probe_ms[2], probe_ms[3], (int)(vramSpaceFree() / 1024));
		memset(probe_ms, 0, sizeof(probe_ms));
	}
	/* GPU probe (three glFinish waits in one frame every 300): debug.txt only */
	probe_frame = n3ds_debug_on() && (state == GS_STATE_GAME_PLAY) && ((frames % 300) == 150);   /* flight only */

	int stereo = 0;
	u64 t_present0 = osGetTime();
	n3ds_phase = "present: pglPresentScreensEx"; n3ds_phase_line = 1072;
	pglPresentScreensEx(stereo, bottom);   /* queues the frame; no GPU or VBlank wait here */
	u64 t_present1 = osGetTime();

	/* back to the engine's target for the next frame. In flight the GPU only renders
	   the 400x240 the top LCD can show (a quarter of the pixels of 800x480): the real
	   hardware is fill-rate bound there, the CPU side takes only a few ms. */
	pglBindTarget(PGL_TARGET_MAIN);
	/* the overlay (panel/dashboard/manual) was drawn into the depth buffer's VRAM: clear
	   the whole depth buffer before the engine's next frame */
	if (pglOverlayAliasesDepth()) {
		n3ds_phase = "present: pglSetRenderScale"; n3ds_phase_line = 1082;
		pglSetRenderScale(1.0f);
		n3ds_phase = "present: glViewport"; n3ds_phase_line = 1083;
		glViewport(0, 0, N3DS_GAME_W, N3DS_GAME_H);
		n3ds_phase = "present: glDisable"; n3ds_phase_line = 1084;
		glDisable(GL_SCISSOR_TEST);
		n3ds_phase = "present: glClearDepth"; n3ds_phase_line = 1085;
		glClearDepth(1.0);
		n3ds_phase = "present: glClear"; n3ds_phase_line = 1086;
		glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	}
	/* Test switch (Nov 16): the file /3ds/freespace2/flight_fullres renders flight at full
	   800x480 too, to see whether the half-scale path is what scrambles the top screen. */
	static int fullres = -1;
	if (fullres < 0) {
		FILE *ff = fopen("sdmc:/3ds/freespace2/flight_fullres", "r");
		fullres = ff ? 1 : 0;
		if (ff) fclose(ff);
		n3ds_log("flight render scale: %s\n", fullres ? "1.0 (flight_fullres file)" : "0.5");
	}
	n3ds_phase = "present: pglSetRenderScale"; n3ds_phase_line = 1097;
	pglSetRenderScale(in_flight() && !fullres ? 0.5f : 1.0f);
	n3ds_phase = "present: pglRestoreViewport"; n3ds_phase_line = 1098;
	pglRestoreViewport();
	n3ds_phase = "present: opengl_state_n3ds_resync"; n3ds_phase_line = 1099;
	opengl_state_n3ds_resync();
	n3ds_phase = "present: n3ds_prof_end"; n3ds_phase_line = 1100;
	n3ds_prof_end(N3DS_PROF_PRESENT);

	u64 t_vb0 = osGetTime(), t_vb1 = t_vb0;

	/* frame timing trace: first frames after start and after the keyboard */
	static u64 t_last = 0;
	extern int n3ds_trace_frames;
	if (n3ds_trace_frames > 0) {
		n3ds_trace_frames--;
		n3ds_log("t frame=%d engine=%d present=%d vblank=%d\n", frames,
			(int)(t_present0 - t_last), (int)(t_present1 - t_present0), (int)(t_vb1 - t_vb0));
	}
	t_last = osGetTime();
}

/* ------------------------------------------------------------------ key names */
/* What to call a game key in training text and directives ("Press $T$" -> "Press Y").
   Mirrors n3ds_keys / n3ds_select_keys above and the cockpit panel buttons. */
struct key_name { int key; const char *name; };
static const key_name Key_names[] = {
	{ KEY_TAB,                 "A" },
	{ KEY_X,                   "B" },
	{ KEY_Y,                   "X" },
	{ KEY_T,                   "Y" },
	{ KEY_LCTRL,               "R" },
	{ KEY_RCTRL,               "R" },
	{ KEY_SPACEBAR,            "L" },
	{ KEY_H,                   "D-Pad Up" },
	{ KEY_M,                   "D-Pad Down" },
	{ KEY_PERIOD,              "D-Pad Left" },
	{ KEY_DIVIDE,              "D-Pad Right" },
	{ KEY_ESC,                 "START" },
	{ KEY_C,                   "SELECT" },
	{ KEY_A,                   "ZR" },
	{ KEY_Z,                   "SELECT+ZR" },
	{ KEY_BACKSP,              "SELECT+ZR (hold)" },
	{ KEY_SLASH,               "ZR (hold)" },
	{ KEY_LBRACKET,            "ZR / SELECT+ZR" },
	{ KEY_RBRACKET,            "ZR / SELECT+ZR" },
	{ KEY_F,                   "SELECT+Y" },
	{ KEY_S,                   "SELECT+X" },
	{ KEY_Q,                   "SELECT+A" },
	{ KEY_R,                   "SELECT+Up" },
	{ KEY_B,                   "SELECT+Down" },
	{ KEY_F3,                  "SELECT+Left" },
	{ KEY_PADPLUS,             "SELECT+Right" },
	{ KEY_HOME,                "the panel's S +" },
	{ KEY_END,                 "the panel's S -" },
	{ KEY_INSERT,              "the panel's W +" },
	{ KEY_DELETE,              "the panel's W -" },
	{ KEY_PAGEUP,              "the panel's E +" },
	{ KEY_PAGEDOWN,            "the panel's E -" },
	{ KEY_SHIFTED | KEY_R,     "COMMS (rearm)" },
	{ KEY_PAUSE,               "SELECT+START" },
};

extern "C++" const char *n3ds_key_name(int keycode)
{
	if (keycode < 0)
		return NULL;
	for (unsigned i = 0; i < sizeof(Key_names) / sizeof(Key_names[0]); i++)
		if (Key_names[i].key == keycode)
			return Key_names[i].name;
	return "the panel / COMMS";   /* no 3DS button: never show a PC key */
}

// Heaps: like libctru's default (32 MB linear, the rest heap) but leave 8 MB uncommitted
// and wait for memory first. With every byte committed, the next app (the test agent or
// the Homebrew Menu) failed to load with D86007F3 in the loader while this process was
// still being torn down; started right after another app, memory may still be held.
extern "C" {
extern u32 __ctru_heap, __ctru_heap_size, __ctru_linear_heap, __ctru_linear_heap_size;
extern char *fake_heap_start, *fake_heap_end;
void __system_allocateHeaps(void)
{
	Handle rl = 0;
	s64 max = 0, cur = 0;
	ResourceLimitType t = RESLIMIT_COMMIT;
	if (R_FAILED(svcGetResourceLimit(&rl, CUR_PROCESS_HANDLE))) svcBreak(USERBREAK_PANIC);
	for (int i = 0; i < 300; i++) {
		svcGetResourceLimitLimitValues(&max, rl, &t, 1);
		svcGetResourceLimitCurrentValues(&cur, rl, &t, 1);
		if (max - cur >= (96ll << 20)) break;
		if (max < (96ll << 20)) break;     /* never that much (an original 3DS): no use waiting (FS-06) */
		svcSleepThread(10000000LL);
	}
	svcCloseHandle(rl);
	const u32 reserve = 8u << 20;
	u32 avail = (u32)(max - cur) & ~0xFFFu;
	avail = avail > reserve * 4 ? avail - reserve : avail;
	u32 linear = 32u << 20;
	if (linear > avail / 3) linear = (avail / 3) & ~0xFFFu;
	u32 heap = (avail - linear) & ~0xFFFu;
	const u32 heap_max = OS_HEAP_AREA_END - OS_HEAP_AREA_BEGIN;
	if (heap > heap_max) heap = heap_max;
	__ctru_heap_size = heap;
	__ctru_linear_heap_size = linear;
	if (R_FAILED(svcControlMemory(&__ctru_heap, OS_HEAP_AREA_BEGIN, 0x0, heap, MEMOP_ALLOC,
	                              (MemPerm)(MEMPERM_READ | MEMPERM_WRITE)))) svcBreak(USERBREAK_PANIC);
	if (R_FAILED(svcControlMemory(&__ctru_linear_heap, 0x0, 0x0, linear, MEMOP_ALLOC_LINEAR,
	                              (MemPerm)(MEMPERM_READ | MEMPERM_WRITE)))) svcBreak(USERBREAK_PANIC);
	mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);
	fake_heap_start = (char *)__ctru_heap;
	fake_heap_end = fake_heap_start + heap;
}
}
