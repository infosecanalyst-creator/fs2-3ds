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
static const u32 BTN_START = KEY_START, BTN_SELECT = KEY_SELECT, BTN_TOUCH = BTN_TOUCH;
static const u32 BTN_DUP = KEY_DUP, BTN_DDOWN = KEY_DDOWN, BTN_DLEFT = KEY_DLEFT, BTN_DRIGHT = KEY_DRIGHT;

#include "n3ds/n3ds_platform.h"
#include "n3ds/n3ds_panel.h"

#include "globalincs/pstypes.h"
#include "io/key.h"
#include "io/mouse.h"
#include "gamesequence/gamesequence.h"
#include "graphics/2d.h"
#include "graphics/gropenglstate.h"

#define FS2_DIR "sdmc:/3ds/freespace2"

/* ------------------------------------------------------------------ logging */

static FILE *n3ds_logfile = NULL;

extern "C" void n3ds_log(const char *fmt, ...)
{
	if (!n3ds_logfile) {
		n3ds_logfile = fopen(FS2_DIR "/fs2_3ds.log", "w");
		if (!n3ds_logfile) return;
	}
	va_list ap;
	va_start(ap, fmt);
	vfprintf(n3ds_logfile, fmt, ap);
	va_end(ap);
	fflush(n3ds_logfile);
}

/* ------------------------------------------------------------------ init */

static int n3ds_quit = 0;
static GLuint n3ds_main_tex = 0;

extern "C" void n3ds_init(void)
{
	osSetSpeedupEnable(true);           /* 804 MHz + L2 cache on New 3DS */
	gfxInitDefault();
	gfxSet3D(true);
	hidInit();

	mkdir(FS2_DIR, 0777);
	chdir(FS2_DIR);

	n3ds_log("FreeSpace 2 3DS starting\n");

	pglInitEx(N3DS_GAME_W, N3DS_GAME_H);
	glGenTextures(1, &n3ds_main_tex);
	pglMainAsTexture(n3ds_main_tex);
	glBindTexture(GL_TEXTURE_2D, 0);
	pglBindTarget(PGL_TARGET_MAIN);
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
static int virtual_throttle = -32768;   /* axis 2: absolute throttle driven by ZL/ZR */

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
	{ BTN_DRIGHT,  KEY_SLASH,                  KEY_RIGHT },   /* cycle secondary */
	{ BTN_START,   KEY_ESC,                    KEY_ESC },     /* in-game menu */
	{ BTN_SELECT,  KEY_C,                      0 },             /* comms (wingman orders) */
};

/* hold SELECT + button for secondary functions in flight */
static const keymap n3ds_select_keys[] = {
	{ BTN_Y,       KEY_F,                      0 },             /* next friendly */
	{ BTN_X,       KEY_S,                      0 },             /* next subsystem */
	{ BTN_A,       KEY_Q,                      0 },             /* equalize shields */
	{ BTN_DUP,     KEY_E,                      0 },             /* target attacker */
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

	/* ZL/ZR drive an absolute throttle so speed stays where you leave it */
	if (held & BTN_ZR) virtual_throttle += 1400;
	if (held & BTN_ZL) virtual_throttle -= 1400;
	if (virtual_throttle > 32767) virtual_throttle = 32767;
	if (virtual_throttle < -32768) virtual_throttle = -32768;
	joy_axes[2] = virtual_throttle;

	joy_hat = 0;
	memset(joy_buttons, 0, sizeof(joy_buttons));
}

/* ------------------------------------------------------------------ screens */

/*
 * Draw a rectangle of the main render target (engine coords, top-left origin) onto the
 * currently bound screen target rectangle (screen coords, top-left origin).
 */
#ifndef N3DS_RT_FLIP_T
#define N3DS_RT_FLIP_T 1
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
	if (flight) {
		/* draw the cockpit panel with the engine into the corner of the main target,
		   then copy it 1:1 to the touchscreen */
		pglBindTarget(PGL_TARGET_MAIN);
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
	pglPresentScreens(stereo);

	/* back to the engine's target for the next frame */
	glBindTexture(GL_TEXTURE_2D, 0);
	pglBindTarget(PGL_TARGET_MAIN);
	opengl_state_n3ds_resync();

	gspWaitForVBlank();
}
