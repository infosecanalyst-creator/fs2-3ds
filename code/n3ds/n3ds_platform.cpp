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

/* libctru's KEY_* button enums clash with the engine's KEY_* scancode macros, so
   capture the button bits under other names before the engine headers are included */
static const u32 BTN_A = KEY_A, BTN_B = KEY_B, BTN_X = KEY_X, BTN_Y = KEY_Y;
static const u32 BTN_L = KEY_L, BTN_R = KEY_R, BTN_ZL = KEY_ZL, BTN_ZR = KEY_ZR;
static const u32 BTN_START = KEY_START, BTN_SELECT = KEY_SELECT, BTN_TOUCH = KEY_TOUCH;
static const u32 BTN_DUP = KEY_DUP, BTN_DDOWN = KEY_DDOWN, BTN_DLEFT = KEY_DLEFT, BTN_DRIGHT = KEY_DRIGHT;

#include "n3ds/n3ds_platform.h"
#include "n3ds/n3ds_panel.h"
#include "n3ds/n3ds_manual.h"

#include "globalincs/pstypes.h"
#include "io/key.h"
#include "io/mouse.h"
#include "gamesequence/gamesequence.h"
#include "graphics/2d.h"

#define FS2_DIR "sdmc:/3ds/freespace2"

/* ------------------------------------------------------------------ logging */

static FILE *n3ds_logfile = NULL;

extern "C" void n3ds_log(const char *fmt, ...)
{
	if (!n3ds_logfile) {
		n3ds_logfile = fopen(FS2_DIR "/fs2_3ds.log", "w");
		if (!n3ds_logfile) return;
	}
	static u64 t0 = 0;
	if (!t0) t0 = osGetTime();
	fprintf(n3ds_logfile, "[%6.1f] ", (double)(osGetTime() - t0) / 1000.0);
	va_list ap;
	va_start(ap, fmt);
	vfprintf(n3ds_logfile, fmt, ap);
	va_end(ap);
	fflush(n3ds_logfile);
}

extern "C" void n3ds_fatal(const char *msg)
{
	n3ds_log("FATAL: %s\n", msg);
	errorConf err;
	errorInit(&err, ERROR_TEXT_WORD_WRAP, CFG_LANGUAGE_EN);
	errorText(&err, msg);
	errorDisp(&err);
}

/* crash handler: log the faulting address and registers, then show a dialog */
static u8 n3ds_exc_stack[0x2000] __attribute__((aligned(8)));
static char n3ds_exc_msg[512];

static void n3ds_exception_handler(ERRF_ExceptionInfo *excep, CpuRegisters *regs)
{
	snprintf(n3ds_exc_msg, sizeof(n3ds_exc_msg),
		"CRASH type %d  pc=%08lx lr=%08lx sp=%08lx far=%08lx\nr0=%08lx r1=%08lx r2=%08lx r3=%08lx\n",
		(int)excep->type, (unsigned long)regs->pc, (unsigned long)regs->lr, (unsigned long)regs->sp,
		(unsigned long)excep->far, (unsigned long)regs->r[0], (unsigned long)regs->r[1],
		(unsigned long)regs->r[2], (unsigned long)regs->r[3]);
	n3ds_log("%s", n3ds_exc_msg);
	if (n3ds_logfile) fclose(n3ds_logfile);
	n3ds_logfile = NULL;
	for (;;) svcSleepThread(1000000000LL);
}

int n3ds_trace_frames = 10;

extern "C" int n3ds_keyboard(int password, const char *initial, char *out, int outlen)
{
	n3ds_trace_frames = 20;
	SwkbdState kb;
	swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, outlen - 1);
	swkbdSetHintText(&kb, password ? "Password" : "Enter text");
	if (initial && initial[0]) swkbdSetInitialText(&kb, initial);
	if (password) swkbdSetPasswordMode(&kb, SWKBD_PASSWORD_HIDE_DELAY);
	swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
	SwkbdButton b = swkbdInputText(&kb, out, outlen);
	n3ds_log("keyboard: button %d text '%s'\n", (int)b, (b == SWKBD_BUTTON_CONFIRM && !password) ? out : "");
	return b == SWKBD_BUTTON_CONFIRM;
}

/* ------------------------------------------------------------------ init */

static int n3ds_quit = 0;
static GLuint n3ds_main_tex = 0;

extern "C" void n3ds_init(void)
{
	osSetSpeedupEnable(true);           /* 804 MHz + L2 cache on New 3DS */
	threadOnException(n3ds_exception_handler, n3ds_exc_stack + sizeof(n3ds_exc_stack), WRITE_DATA_TO_HANDLER_STACK);
	gfxInitDefault();
	gfxSet3D(true);
	hidInit();

	mkdir(FS2_DIR, 0777);
	chdir(FS2_DIR);

	n3ds_log("FreeSpace 2 3DS starting\n");
	n3ds_log("heap %lu KB, linear %lu KB free\n", (unsigned long)(envGetHeapSize() / 1024), (unsigned long)(linearSpaceFree() / 1024));

	pglInitEx(N3DS_GAME_W, N3DS_GAME_H);
	glGenTextures(1, &n3ds_main_tex);
	pglMainAsTexture(n3ds_main_tex);
	glBindTexture(GL_TEXTURE_2D, 0);
	pglBindTarget(PGL_TARGET_MAIN);
	n3ds_log("graphics ready\n");
}

extern "C" void n3ds_shutdown(void)
{
	n3ds_log("shutdown\n");
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
	{ BTN_A,       KEY_TAB,                    KEY_ENTER },   /* afterburner / accept */
	{ BTN_B,       KEY_X,                      KEY_ESC },     /* countermeasure / back */
	{ BTN_X,       KEY_Y,                      0 },             /* target ship in reticle */
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
	{ BTN_ZL,      KEY_Z,                      0 },             /* throttle down (hold) */
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

extern "C" void n3ds_input_poll(void)
{
	if (!aptMainLoop()) {
		n3ds_quit = 1;
		gameseq_post_event(GS_EVENT_QUIT_GAME);
		return;
	}

	hidScanInput();
	u32 held = hidKeysHeld();
	u32 down = hidKeysDown();
	u32 up = hidKeysUp();
	if (down) {
		touchPosition ltp; hidTouchRead(&ltp);
		n3ds_log("input down %08lx touch %d,%d\n", (unsigned long)down, ltp.px, ltp.py);
	}

	int flight = in_flight();
	static int was_flight = -1;
	if (flight != was_flight) {
		release_all_keys();
		was_flight = flight;
	}

	/* panel buttons take the touch while flying */
	touchPosition tp;
	hidTouchRead(&tp);
	if (flight) {
		n3ds_panel_touch((held & BTN_TOUCH) ? 1 : 0, tp.px, tp.py, (down & BTN_TOUCH) ? 1 : 0);
		touch_down = 0;
	} else {
		if (held & BTN_TOUCH) {
			/* bottom screen shows the whole 800x480 frame squeezed into 320x240 */
			touch_x = tp.px * N3DS_GAME_W / 320;
			touch_y = tp.py * N3DS_GAME_H / 240;
		}
		int now = (held & BTN_TOUCH) ? 1 : 0;
		if (now != touch_down) {
			/* make sure the cursor position is updated before the click lands */
			mouse_mark_button(MOUSE_LEFT_BUTTON, now);
			touch_down = now;
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
		if (down & (BTN_R | BTN_DRIGHT)) n3ds_manual_page(1);
		if (down & (BTN_L | BTN_DLEFT)) n3ds_manual_page(-1);
		if (down & BTN_B) {
			n3ds_manual_close();
			if (flight) { key_mark(KEY_PAUSE, 1, 0); key_mark(KEY_PAUSE, 0, 0); }
		}
		return;
	}

	/* buttons */
	int select_held = (held & BTN_SELECT) && flight;
	static int select_used = 0;
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
	joy_axes[0] = scale_axis(cp.dx, 150);
	joy_axes[1] = -scale_axis(cp.dy, 150);
	joy_axes[3] = scale_axis(cs.dx, 140);   /* bank */
	joy_axes[4] = -scale_axis(cs.dy, 140);

	joy_axes[2] = 0;

	joy_hat = 0;
	memset(joy_buttons, 0, sizeof(joy_buttons));
}

/* ------------------------------------------------------------------ screens */

/*
 * Draw a rectangle of the main render target (engine coords, top-left origin) onto the
 * currently bound screen target rectangle (screen coords, top-left origin).
 */
#ifndef N3DS_RT_FLIP_T
#define N3DS_RT_FLIP_T 0
#endif

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

static void compose_begin(int target)
{
	pglBindTarget(target);
	int tw, th;
	pglGetTargetSize(&tw, &th);

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
	glOrtho(0, tw, 0, th, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);

	glActiveTexture(GL_TEXTURE1);
	glDisable(GL_TEXTURE_2D);
	glActiveTexture(GL_TEXTURE0);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, n3ds_main_tex);
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
	if ((frames % 300) == 0)
		n3ds_log("frame %d state %d touch %d,%d\n", frames, state, touch_x, touch_y);

	pglSaveViewport();

	/* ---- top screen */
	compose_begin(PGL_TARGET_TOP);
	if (flight) {
		/* whole frame, 2:1 downscale with bilinear filtering */
		blit_main(0, 0, 400, 240, 0, 0, N3DS_GAME_W, N3DS_GAME_H);
	} else {
		/* 1:1 magnifier centred on the stylus */
		int cx = touch_x - 200, cy = touch_y - 120;
		if (cx < 0) cx = 0;
		if (cy < 0) cy = 0;
		if (cx > N3DS_GAME_W - 400) cx = N3DS_GAME_W - 400;
		if (cy > N3DS_GAME_H - 240) cy = N3DS_GAME_H - 240;
		blit_main(0, 0, 400, 240, (float)cx, (float)cy, (float)(cx + 400), (float)(cy + 240));
	}
	compose_end();

	/* ---- bottom screen */
	if (n3ds_manual_is_open()) {
		pglBindTarget(PGL_TARGET_MAIN);
		pglRestoreViewport();
		opengl_state_n3ds_resync();
		n3ds_manual_draw();
		compose_begin(PGL_TARGET_BOTTOM);
		blit_main(0, 0, 320, 240, 0, 0, 320, 240);
		compose_end();
	} else if (flight) {
		/* draw the cockpit panel with the engine into the corner of the main target,
		   then copy it 1:1 to the touchscreen */
		pglBindTarget(PGL_TARGET_MAIN);
		pglRestoreViewport();
		opengl_state_n3ds_resync();
		n3ds_panel_draw();
		compose_begin(PGL_TARGET_BOTTOM);
		blit_main(0, 0, 320, 240, 0, 0, 320, 240);
		compose_end();
	} else {
		compose_begin(PGL_TARGET_BOTTOM);
		blit_main(0, 0, 320, 240, 0, 0, N3DS_GAME_W, N3DS_GAME_H);
		compose_end();
	}

	int stereo = 0;
	u64 t_present0 = osGetTime();
	pglPresentScreens(stereo);
	u64 t_present1 = osGetTime();

	/* back to the engine's target for the next frame */
	pglBindTarget(PGL_TARGET_MAIN);
	pglRestoreViewport();
	opengl_state_n3ds_resync();

	u64 t_vb0 = osGetTime();
	gspWaitForVBlank();
	u64 t_vb1 = osGetTime();

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
