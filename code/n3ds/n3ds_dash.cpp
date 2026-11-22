/*
 * Top-screen dashboard for FreeSpace 2 menus on the New 3DS.
 *
 * In menus the touchscreen shows the whole menu (the game itself) and the top screen
 * shows information that is NOT on the touchscreen: where you are, your pilot and
 * campaign, the name of whatever the pointer is over, and how to work this screen.
 *
 *   +--------------------------------------------------+
 *   | SCREEN NAME                              12:34   |
 *   |--------------------------------------------------|
 *   | PILOT  callsign   rank   kills / missions         |
 *   | CAMPAIGN  name   next mission   n of N flown      |
 *   |--------------------------------------------------|
 *   | POINTER  >  label of the room / item under it      |
 *   |--------------------------------------------------|
 *   | help for this screen (4-6 lines)                  |
 *   |--------------------------------------------------|
 *   | controls footer                                    |
 *   +--------------------------------------------------+
 */
#include "n3ds/n3ds_panel.h"
#include "n3ds/n3ds_dash.h"

#include <time.h>
#include <string.h>

#include "globalincs/pstypes.h"
#include "graphics/2d.h"
#include "graphics/font.h"
#include "gamesequence/gamesequence.h"
#include "playerman/player.h"
#include "stats/scoring.h"
#include "mission/missioncampaign.h"
#include "popup/popup.h"

/* FS2-U2: the dashboard is 320 wide on the touchscreen and 400 wide when the screens are swapped
   and it is on the top screen (it used to sit there as a 320 column between two black bars) */
static int Dash_w = 320;
#define DASH_W Dash_w
#define DASH_H 240

static char Hover_text[128] = "";
static int Hover_frame = -100;
static int Dash_frame = 0;

void n3ds_dash_hover(const char *text)
{
	if (!text) return;
	strncpy(Hover_text, text, sizeof(Hover_text) - 1);
	Hover_text[sizeof(Hover_text) - 1] = 0;
	Hover_frame = Dash_frame;
}

struct screen_help {
	int state;
	const char *title;
	const char *lines[6];
};

static const screen_help Help[] = {
	{ GS_STATE_INITIAL_PLAYER_SELECT, "PILOT SELECT", {
		"Pick your pilot, then Select (lower right).",
		"New pilot: Create, then type a callsign.", NULL } },
	{ GS_STATE_MAIN_MENU, "MAIN HALL", {
		"READY ROOM starts or continues the campaign.",
		"Or point at a room and press A to go in.",
		"Options: lower detail if battles feel slow.", NULL } },
	{ GS_STATE_CMD_BRIEF, "COMMAND BRIEFING", {
		"CONTINUE goes to the briefing.",
		"Arrows step through the pages.", NULL } },
	{ GS_STATE_BRIEFING, "MISSION BRIEFING", {
		"COMMIT launches the mission.",
		"SHIPS and WEAPONS open the loadout pages.",
		"Arrows step through the briefing stages.", NULL } },
	{ GS_STATE_SHIP_SELECT, "SHIP SELECTION", {
		"COMMIT launches the mission.",
		"Drag a ship from the pool onto a wing slot.", NULL } },
	{ GS_STATE_WEAPON_SELECT, "WEAPON LOADOUT", {
		"COMMIT launches the mission.",
		"Drag weapons onto your ship's banks.", NULL } },
	{ GS_STATE_DEBRIEF, "DEBRIEFING", {
		"ACCEPT records the result and goes on.",
		"NEXT TAB shows your statistics.", NULL } },
	{ GS_STATE_OPTIONS_MENU, "OPTIONS", {
		"Detail tab: lower it for smoother battles.",
		"Controls are fixed - see the manual.", NULL } },
	{ GS_STATE_CONTROL_CONFIG, "CONTROLS", {
		"The 3DS buttons are mapped for you.",
		"The MANUAL button lists them all.", NULL } },
	{ GS_STATE_HUD_CONFIG, "HUD CONFIG", {
		"Click a gauge to toggle it.", NULL } },
	{ GS_STATE_TECH_MENU, "TECH ROOM", {
		"Ship and weapon data, intel, cutscenes.", NULL } },
	{ GS_STATE_BARRACKS_MENU, "BARRACKS", {
		"Pilots, squadron logos and service records.",
		"Make Active switches pilot.", NULL } },
	{ GS_STATE_CAMPAIGN_ROOM, "CAMPAIGN ROOM", {
		"Choose or restart a campaign.", NULL } },
	{ GS_STATE_SIMULATOR_ROOM, "SIMULATOR", {
		"Replay missions you have flown.", NULL } },
	{ GS_STATE_TRAINING_MENU, "TRAINING", { NULL } },
	{ GS_STATE_GAME_PAUSED, "PAUSED", {
		"START or B returns to the cockpit.", NULL } },
	{ GS_STATE_TRAINING_PAUSED, "PAUSED", {
		"START or B returns to the cockpit.", NULL } },
	{ GS_STATE_VIEW_MEDALS, "MEDALS", { NULL } },
	{ GS_STATE_HOTKEY_SCREEN, "HOTKEYS", { NULL } },
	{ GS_STATE_SHOW_GOALS, "MISSION GOALS", { NULL } },
	{ GS_STATE_MISSION_LOG_SCROLLBACK, "MISSION LOG", { NULL } },
	{ GS_STATE_RED_ALERT, "RED ALERT", {
		"No briefing - CONTINUE launches.", NULL } },
	{ GS_STATE_FICTION_VIEWER, "FICTION", { NULL } },
	{ GS_STATE_GAMEPLAY_HELP, "HELP", { NULL } },
	{ GS_STATE_LOOP_BRIEF, "SPECIAL MISSION", { NULL } },
	{ GS_STATE_END_OF_CAMPAIGN, "END OF CAMPAIGN", { NULL } },
	{ GS_STATE_DEATH_DIED, "SHIP DESTROYED", { NULL } },
	{ GS_STATE_DEATH_BLEW_UP, "SHIP DESTROYED", { NULL } },
	{ GS_STATE_CREDITS, "CREDITS", { NULL } },
	{ GS_STATE_LAB, "LAB", { NULL } },
};
#define NUM_HELP ((int)(sizeof(Help) / sizeof(Help[0])))

static void hline(int y)
{
	gr_set_color(120, 150, 230);
	n3ds_rect(4, y, DASH_W - 8, 1);
}

static void label(int x, int y, const char *text)
{
	gr_set_color(150, 150, 165);
	gr_string(x, y, text, false);
}

static void value(int x, int y, const char *text)
{
	gr_set_color(235, 235, 235);
	gr_string(x, y, text, false);
}

/* draw text word-wrapped to the dashboard width; returns the y below it */
static int Wrap_bottom = 240;

static int wrapped(int x, int y, const char *text, int lh)
{
	char line[160];
	int maxw = DASH_W - x - 6;
	const char *p = text;
	while (*p) {
		int n = 0, last_space = -1;
		while (p[n]) {
			if (p[n] == ' ') last_space = n;
			int len = n + 1;
			if (len >= (int)sizeof(line)) break;
			memcpy(line, p, len); line[len] = 0;
			int w, h;
			gr_get_string_size(&w, &h, line);
			if (w > maxw) break;
			n++;
		}
		int take = n;
		if (p[n] && last_space > 0) take = last_space;
		if (take <= 0) take = 1;
		memcpy(line, p, take); line[take] = 0;
		gr_string(x, y, line, false);
		y += lh;
		p += take;
		while (*p == ' ') p++;
		if (y + lh > Wrap_bottom) break;
	}
	return y;
}

/* how many lines wrapped() would use for text at x */
static int wrapped_lines(int x, const char *text)
{
	int save = Wrap_bottom;
	Wrap_bottom = 1 << 20;
	/* measure by drawing off-screen-free: replicate the wrap without drawing */
	char line[160];
	int maxw = DASH_W - x - 6, n = 0;
	const char *p = text;
	while (*p) {
		int len = 0, last_space = -1;
		while (p[len]) {
			if (p[len] == ' ') last_space = len;
			if (len + 1 >= (int)sizeof(line)) break;
			memcpy(line, p, len + 1); line[len + 1] = 0;
			int w, h;
			gr_get_string_size(&w, &h, line);
			if (w > maxw) break;
			len++;
		}
		int take = len;
		if (p[len] && last_space > 0) take = last_space;
		if (take <= 0) take = 1;
		p += take;
		while (*p == ' ') p++;
		n++;
	}
	Wrap_bottom = save;
	return n;
}

/* copy a campaign mission file name without its extension */
static void mission_label(char *out, int outlen, const char *name)
{
	strncpy(out, name ? name : "", outlen - 1);
	out[outlen - 1] = 0;
	char *dot = strrchr(out, '.');
	if (dot) *dot = 0;
}

/* ---- touch buttons on the dashboard */
struct dash_button { int x, y, w, h; const char *label; };
#define BTN_Y 180
#define BTN_H 26
#define ACT_Y 150                  /* the screen's own action row, 4 px above the fixed row */
/* the fixed row: BACK ENTER ZOOM MANUAL (rule 11: 4 px apart, 6 px from the edges, as wide as fits) */
static const char *const Base_labels[4] = { "BACK", "ENTER", "ZOOM", "MANUAL" };

/* FS2-X2: the main actions of a screen as real buttons, in a row above the fixed one. A first-time
   player no longer has to find Commit ("lower right") or the loadout tabs with the pointer. Each
   sends the key the game itself reads for that action, or posts the event a click on the room posts. */
enum { ACT_KEY, ACT_EVENT, ACT_READY_ROOM };
struct screen_action { const char *label; int kind; int arg; };
struct screen_actions { int state; int count; screen_action a[4]; };
#include "io/key.h"
#include "globalincs/systemvars.h"
static const screen_actions Actions[] = {
	{ GS_STATE_MAIN_MENU, 3, { { "READY ROOM", ACT_READY_ROOM, 0 }, { "TECH ROOM", ACT_EVENT, GS_EVENT_TECH_MENU }, { "OPTIONS", ACT_EVENT, GS_EVENT_OPTIONS_MENU } } },
	{ GS_STATE_CMD_BRIEF, 1, { { "CONTINUE", ACT_KEY, KEY_CTRLED | KEY_ENTER } } },
	{ GS_STATE_FICTION_VIEWER, 1, { { "CONTINUE", ACT_KEY, KEY_CTRLED | KEY_ENTER } } },
	{ GS_STATE_RED_ALERT, 1, { { "CONTINUE", ACT_KEY, KEY_CTRLED | KEY_ENTER } } },
	{ GS_STATE_BRIEFING, 4, { { "BRIEFING", ACT_KEY, KEY_B }, { "SHIPS", ACT_KEY, KEY_S }, { "WEAPONS", ACT_KEY, KEY_W }, { "COMMIT", ACT_KEY, KEY_CTRLED | KEY_ENTER } } },
	{ GS_STATE_SHIP_SELECT, 4, { { "BRIEFING", ACT_KEY, KEY_B }, { "SHIPS", ACT_KEY, KEY_S }, { "WEAPONS", ACT_KEY, KEY_W }, { "COMMIT", ACT_KEY, KEY_CTRLED | KEY_ENTER } } },
	{ GS_STATE_WEAPON_SELECT, 4, { { "BRIEFING", ACT_KEY, KEY_B }, { "SHIPS", ACT_KEY, KEY_S }, { "WEAPONS", ACT_KEY, KEY_W }, { "COMMIT", ACT_KEY, KEY_CTRLED | KEY_ENTER } } },
	{ GS_STATE_DEBRIEF, 2, { { "NEXT TAB", ACT_KEY, KEY_TAB }, { "ACCEPT", ACT_KEY, KEY_CTRLED | KEY_ENTER } } },
};
#define NUM_ACTIONS ((int)(sizeof(Actions) / sizeof(Actions[0])))

#define MAX_DASH_BUTTONS 8
static dash_button Dash_buttons[MAX_DASH_BUTTONS];     /* this frame's buttons: the action row, then the fixed row */
static const screen_actions *Dash_actions = NULL;
static int Dash_nact = 0;          /* buttons in the action row */
static int Dash_nbuttons = 4;
static const char *Dash_pressed_label = NULL;
static int Dash_pressed = -1;
static int Dash_nav = 0;           /* the dashboard is on the top screen: highlight shown */
static int Dash_frow = 1, Dash_fcol = 0;   /* highlighted button: row (0 action, 1 fixed) and column */
static int Dash_flash = 0;         /* frames left of an A press's animation */

/* one row of n buttons filling the width */
static void dash_row(dash_button *out, int n, int y)
{
	int w = (DASH_W - 12 - 4 * (n - 1)) / n;
	for (int i = 0; i < n; i++) { out[i].x = 6 + i * (w + 4); out[i].y = y; out[i].w = w; out[i].h = BTN_H; }
}

/* lay the buttons out for the screen the game is on now */
static void dash_layout(void)
{
	int state = gameseq_get_state();
	Dash_actions = NULL;
	for (int i = 0; i < NUM_ACTIONS; i++)
		if (Actions[i].state == state) { Dash_actions = &Actions[i]; break; }
	Dash_nact = Dash_actions ? Dash_actions->count : 0;
	if (Dash_nact) {
		dash_row(Dash_buttons, Dash_nact, ACT_Y);
		for (int i = 0; i < Dash_nact; i++) Dash_buttons[i].label = Dash_actions->a[i].label;
	}
	dash_row(Dash_buttons + Dash_nact, 4, BTN_Y);
	for (int i = 0; i < 4; i++) Dash_buttons[Dash_nact + i].label = Base_labels[i];
	Dash_nbuttons = Dash_nact + 4;
	if (Dash_frow == 0 && !Dash_nact) { Dash_frow = 1; Dash_fcol = 0; }
	int cnt = Dash_frow == 0 ? Dash_nact : 4;
	if (Dash_fcol >= cnt) Dash_fcol = cnt - 1;
	if (Dash_fcol < 0) Dash_fcol = 0;
}

static int dash_focus_index(void) { return Dash_frow == 0 ? Dash_fcol : Dash_nact + Dash_fcol; }

extern "C" void n3ds_click(void);
extern "C" void n3ds_menu_zoom(int delta);
extern "C" void n3ds_tap_key(int key);
extern "C" void n3ds_set_debug(int on);
extern "C" int n3ds_marker_failed(void);
#include "n3ds/n3ds_manual.h"
#include "n3ds/n3ds_platform.h"
#ifndef PORT_VERSION
#define PORT_VERSION "unknown"
#endif

static void dash_action(int i)
{
	if (i < 0 || i >= Dash_nbuttons) return;
	if (i < Dash_nact) {
		const screen_action &a = Dash_actions->a[i];
		switch (a.kind) {
			case ACT_KEY: key_outkey(a.arg); break;
			case ACT_EVENT: gameseq_post_event(a.arg); break;
			case ACT_READY_ROOM:
				/* what a click on the Ready Room door does (mainhallmenu.cpp, READY_ROOM_REGION) */
				if (Player) Player->flags &= ~PLAYER_FLAGS_IS_MULTI;
				Game_mode = GM_NORMAL;
				gameseq_post_event(GS_EVENT_NEW_CAMPAIGN);
				break;
		}
		return;
	}
	switch (i - Dash_nact) {
		case 0: n3ds_tap_key(KEY_ESC); break;
		case 1: n3ds_tap_key(KEY_ENTER); break;
		case 2: n3ds_menu_zoom(0); break;      /* 0 = cycle */
		case 3: n3ds_manual_toggle(); break;
	}
}

int n3ds_dash_button_at(int x, int y)
{
	dash_layout();
	for (int i = 0; i < Dash_nbuttons; i++) {
		const dash_button &b = Dash_buttons[i];
		if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h)
			return i;
	}
	return -1;
}

void n3ds_dash_set_nav(int on) { Dash_nav = on; Dash_w = on ? 400 : 320; }

/* Left / Right move the highlight along a row, Up / Down change row; A presses it - the button
   sinks and lights for a few frames (as when tapped) and then acts. A press during that time is
   ignored, never saved for later. */
void n3ds_dash_nav(int dx, int dy, int press)
{
	if (n3ds_manual_is_open()) { Dash_flash = 0; return; }   /* a press is dropped, not saved */
	dash_layout();
	if (Dash_flash > 0) {
		if (--Dash_flash == 0) dash_action(dash_focus_index());
		return;
	}
	if (!Dash_nav) return;
	int cnt = Dash_frow == 0 ? Dash_nact : 4;
	if (dx < 0 && Dash_fcol > 0) Dash_fcol--;
	if (dx > 0 && Dash_fcol < cnt - 1) Dash_fcol++;
	if ((dy < 0 && Dash_frow == 1 && Dash_nact) || (dy > 0 && Dash_frow == 0)) {
		/* the other row: the button whose middle is nearest */
		const dash_button &cur = Dash_buttons[dash_focus_index()];
		int cx = cur.x + cur.w / 2, row = 1 - Dash_frow;
		int first = row == 0 ? 0 : Dash_nact, n = row == 0 ? Dash_nact : 4, best = 0, bd = 1 << 20;
		for (int i = 0; i < n; i++) {
			int d = Dash_buttons[first + i].x + Dash_buttons[first + i].w / 2 - cx;
			if (d < 0) d = -d;
			if (d < bd) { bd = d; best = i; }
		}
		Dash_frow = row; Dash_fcol = best;
	}
	if (press) {
		n3ds_click();
		Dash_flash = 6;
	}
}

void n3ds_dash_touch(int held, int x, int y, int pressed)
{
	if (pressed) {
		Dash_pressed = n3ds_dash_button_at(x, y);
		Dash_pressed_label = Dash_pressed >= 0 ? Dash_buttons[Dash_pressed].label : NULL;
		if (Dash_pressed >= 0) n3ds_click();
		return;
	}
	if (!held && Dash_pressed >= 0) {
		/* act on release, like a real button (not if the screen changed under the stylus) */
		dash_layout();
		if (Dash_pressed < Dash_nbuttons && Dash_buttons[Dash_pressed].label == Dash_pressed_label)
			dash_action(Dash_pressed);
		Dash_pressed = -1;
	}
}

static void draw_buttons(void)
{
	/* house style: face 40,56,120, edge 120,150,230, shadow line, pressed 220,180,60, gold focus ring */
	for (int i = 0; i < Dash_nbuttons; i++) {
		const dash_button &b = Dash_buttons[i];
		int focused = Dash_nav && i == dash_focus_index();
		int pressed = (i == Dash_pressed) || (focused && Dash_flash > 0);
		int off = pressed ? 1 : 0;       /* pressed buttons sink by a pixel */
		if (pressed) gr_set_color(220, 180, 60); else gr_set_color(40, 56, 120);
		n3ds_rect(b.x, b.y + off, b.w, b.h);
		gr_set_color(120, 150, 230);
		n3ds_rect(b.x, b.y + off, b.w, 1); n3ds_rect(b.x, b.y + off, 1, b.h); n3ds_rect(b.x + b.w - 1, b.y + off, 1, b.h);
		gr_set_color(20, 30, 70);
		n3ds_rect(b.x, b.y + off + b.h - 1, b.w, 1);
		if (focused) {
			gr_set_color(255, 204, 64);
			n3ds_rect(b.x, b.y + off, b.w, 2);
			n3ds_rect(b.x, b.y + off + b.h - 2, b.w, 2);
			n3ds_rect(b.x, b.y + off, 2, b.h);
			n3ds_rect(b.x + b.w - 2, b.y + off, 2, b.h);
		}
		int w, h;
		gr_get_string_size(&w, &h, b.label);
		gr_set_color(240, 240, 240);
		gr_string(b.x + (b.w - w) / 2, b.y + off + (b.h - h) / 2, b.label, false);
	}
}

/* a house button (START menu, message page): face, edge, shadow, gold ring when focused, sunk and
   gold while pressed */
static void house_button(int x, int y, int w, int h, const char *text, int pressed, int focused)
{
	int off = pressed ? 1 : 0;
	if (pressed) gr_set_color(220, 180, 60); else gr_set_color(40, 56, 120);
	n3ds_rect(x, y + off, w, h);
	gr_set_color(120, 150, 230);                 /* edge */
	n3ds_rect(x, y + off, w, 1); n3ds_rect(x, y + off, 1, h); n3ds_rect(x + w - 1, y + off, 1, h);
	gr_set_color(20, 30, 70);                    /* shadow */
	n3ds_rect(x, y + off + h - 1, w, 1);
	if (focused) {                               /* gold focus ring */
		gr_set_color(255, 204, 64);
		n3ds_rect(x, y + off, w, 2); n3ds_rect(x, y + off + h - 2, w, 2);
		n3ds_rect(x, y + off, 2, h); n3ds_rect(x + w - 2, y + off, 2, h);
	}
	/* the label always fits: one too wide for the button loses its end to "..." */
	char fit[96];
	snprintf(fit, sizeof(fit), "%s", text);
	int tw, th; gr_get_string_size(&tw, &th, fit);
	for (int len = (int)strlen(fit); tw > w - 8 && len > 1; ) {
		len--;
		snprintf(fit + len, sizeof(fit) - len, "...");
		gr_get_string_size(&tw, &th, fit);
	}
	gr_set_color(240, 240, 240);
	gr_string(x + (w - tw) / 2, y + off + (h - th) / 2, fit, false);
}

/* the gold hint strip (y 224, 16 px): the first of the hints that fits the screen */
static void hint_strip(int sw, const char *const *hints, int n)
{
	int w = 0, h = 9;
	gr_set_color(4, 6, 20);
	n3ds_rect(0, 224, sw, 16);
	const char *hint = hints[n - 1];
	for (int i = 0; i < n; i++) {
		gr_get_string_size(&w, &h, hints[i]);
		if (w <= sw - 8) { hint = hints[i]; break; }
	}
	gr_get_string_size(&w, &h, hint);
	gr_set_color(255, 204, 64);
	gr_string((sw - w) / 2, 224 + (16 - h) / 2, hint, false);
}

static void centred(int sw, int y, const char *t)
{
	int w, h;
	gr_get_string_size(&w, &h, t);
	gr_string((sw - w) / 2, y, t, false);
}

/* ---- START port menu (house style: claude/3ds-look-and-feel.md) */
int n3ds_exit_requested = 0;
static int Pm_open = 0;
static int Pm_page = 0;            /* 0 main, 1 exit confirm, 2 settings */
static int Pm_focus = 0;
static int Pm_pressed = -1;        /* button under the stylus */
static int Pm_flash = 0;           /* frames of an A press's animation */
#define PM_BH 30
#define PM_GAP 6
/* FS-08: one START menu in flight and out. In flight it is drawn on the cockpit panel's screen
   and the platform code pauses the game while it is open; it has one more row, END MISSION, which
   brings up the game's own "end the mission?" question (No / Yes, Quit / Yes, Restart) - shown as
   big buttons by the message page below (FS2-U1, FS2-X3).
   FS2-U5: the rows are as wide as the screen (6 px from the edges), and there is a SETTINGS page:
   LEFT HAND and DEBUG LOG (off unless switched on here; it no longer follows a debug.txt file). */
static int Pm_flight = 0;
int n3ds_pm_game_menu_requested = 0;     /* set by END MISSION: the platform sends Esc after the menu closes */
static const char *Pm_main[] = { "RESUME", "MANUAL", "SETTINGS", "END MISSION", "EXIT GAME" };
static const char *Pm_main_menus[] = { "RESUME", "MANUAL", "SETTINGS", "EXIT GAME" };
/* the label of row i on the main page */
static const char *pm_label(int i) { return Pm_flight ? Pm_main[i] : Pm_main_menus[i]; }
static int Pm_lx = 0, Pm_ly = 0;         /* last stylus position while held (a release has no position) */
static int Pm_over = 0;                  /* the stylus is still on the button it went down on */
static const char *Pm_confirm[] = { "YES", "NO" };
/* the menu is 320 wide in flight (the panel's screen, centred when that is the top one) and as
   wide as the dashboard in menus */
#define PM_SW (Pm_flight ? 320 : DASH_W)

int n3ds_portmenu_is_open(void) { return Pm_open; }
void n3ds_portmenu_open(int in_flight) { Pm_open = 1; Pm_page = 0; Pm_focus = 0; Pm_pressed = -1; Pm_flash = 0; Pm_flight = in_flight ? 1 : 0; Pm_over = 0; }
void n3ds_portmenu_close(void) { Pm_open = 0; Pm_pressed = -1; Pm_flash = 0; }

static int pm_count(void) { return Pm_page == 0 ? (Pm_flight ? 5 : 4) : Pm_page == 1 ? 2 : 3; }
#define PM_EXIT_ROW (Pm_flight ? 4 : 3)
#define PM_ROWS_Y0 (Pm_flight ? 28 : 44)    /* five rows start at the top; four leave room for the MENU caption */

/* button rectangles: rows across the screen; the confirm page's two side by side */
static void pm_rect(int i, int *x, int *y, int *w, int *h)
{
	if (Pm_page == 1) {
		*w = (PM_SW - 12 - 4) / 2; *h = 36; *y = 140;
		*x = 6 + i * (*w + 4);
	} else {
		*x = 6; *w = PM_SW - 12; *h = PM_BH;
		*y = (Pm_page == 0 ? PM_ROWS_Y0 : 44) + i * (PM_BH + PM_GAP);
	}
}

static int pm_at(int tx, int ty)
{
	for (int i = 0; i < pm_count(); i++) {
		int x, y, w, h; pm_rect(i, &x, &y, &w, &h);
		if (tx >= x && tx < x + w && ty >= y && ty < y + h) return i;
	}
	return -1;
}

static void pm_act(int i)
{
	if (Pm_page == 0) {
		if (i == 0) n3ds_portmenu_close();
		else if (i == 1) { n3ds_portmenu_close(); n3ds_manual_toggle(); }
		else if (i == 2) { Pm_page = 2; Pm_focus = 0; }
		else if (i == PM_EXIT_ROW) { Pm_page = 1; Pm_focus = 1; }       /* focus starts on NO */
		else { n3ds_portmenu_close(); n3ds_pm_game_menu_requested = 1; }
	} else if (Pm_page == 1) {
		if (i == 0) { n3ds_portmenu_close(); n3ds_exit_requested = 1; }
		else { Pm_page = 0; Pm_focus = PM_EXIT_ROW; }
	} else {
		if (i == 0) n3ds_set_lefty(!n3ds_lefty());
		else if (i == 1) n3ds_set_debug(!n3ds_debug_on());
		else { Pm_page = 0; Pm_focus = 2; }
	}
}

void n3ds_portmenu_input(int dy, int a, int b, int start, int touch_held, int tx, int ty, int touch_pressed)
{
	if (!Pm_open) return;
	if (Pm_flash > 0) {                          /* finish an A press; new presses are dropped */
		if (--Pm_flash == 0) pm_act(Pm_focus);
		return;
	}
	if (touch_pressed) {
		Pm_pressed = pm_at(tx, ty);
		Pm_lx = tx; Pm_ly = ty; Pm_over = Pm_pressed >= 0;
		if (Pm_pressed >= 0) { Pm_focus = Pm_pressed; n3ds_click(); }
		return;
	}
	if (touch_held) {                            /* FS-08: sliding off the button un-presses it */
		Pm_lx = tx; Pm_ly = ty;
		Pm_over = Pm_pressed >= 0 && pm_at(tx, ty) == Pm_pressed;
		return;
	}
	if (Pm_pressed >= 0) {                       /* act on release, like a real button; released outside = cancelled */
		int i = Pm_pressed; Pm_pressed = -1;
		if (Pm_over && pm_at(Pm_lx, Pm_ly) == i) pm_act(i);
		Pm_over = 0;
		return;
	}
	if (b || start) {
		n3ds_click();
		if (Pm_page == 1) { Pm_page = 0; Pm_focus = PM_EXIT_ROW; }
		else if (Pm_page == 2) { Pm_page = 0; Pm_focus = 2; }
		else n3ds_portmenu_close();
		return;
	}
	int n = pm_count();
	if (dy) Pm_focus = (Pm_focus + dy + n) % n;
	if (a) { n3ds_click(); Pm_flash = 6; }
}

static void pm_button(int i, const char *text)
{
	int x, y, w, h; pm_rect(i, &x, &y, &w, &h);
	int pressed = (i == Pm_pressed && Pm_over) || (i == Pm_focus && Pm_flash > 0);
	house_button(x, y, w, h, text, pressed, i == Pm_focus && Pm_pressed < 0);
}

static void pm_draw(void)
{
	const int sw = PM_SW;
	gr_set_color(8, 12, 40);                     /* navy body under the header bar */
	n3ds_rect(0, 22, sw, DASH_H - 22);
	gr_set_font(FONT1);
	if (Pm_page == 0) {
		if (!Pm_flight) {
			gr_set_color(255, 204, 64);
			centred(sw, 29, "MENU");
		}
		for (int i = 0; i < pm_count(); i++) pm_button(i, pm_label(i));
		const char *v = "Build " PORT_VERSION;   /* port version (git describe of fs2-3ds) */
		gr_set_color(150, 150, 165);
		centred(sw, Pm_flight ? 209 : 196, v);
	} else if (Pm_page == 1) {
		/* FS2-Q2: say what YES really does. The pilot file is written before the game closes
		   (n3ds_input_poll); a mission in flight cannot be saved by this engine. */
		const int pilot = Player && Player->callsign[0];
		gr_set_color(255, 204, 64);
		centred(sw, 60, "EXIT FREESPACE 2?");
		gr_set_color(240, 240, 240);
		centred(sw, 84, pilot ? "Your pilot is saved first." : "There is no pilot to save yet.");
		if (Pm_flight) centred(sw, 100, "The mission you are flying is not saved.");
		for (int i = 0; i < 2; i++) pm_button(i, Pm_confirm[i]);
	} else {
		gr_set_color(255, 204, 64);
		centred(sw, 29, "SETTINGS");
		pm_button(0, n3ds_lefty() ? "LEFT HAND: ON" : "LEFT HAND: OFF");
		pm_button(1, n3ds_debug_on() ? "DEBUG LOG: ON" : "DEBUG LOG: OFF");
		pm_button(2, "BACK");
		gr_set_color(150, 150, 165);
		centred(sw, 158, "The full debug log goes to fs2_3ds.log");
		centred(sw, 171, "on the SD card. The game runs slower.");
		if (n3ds_marker_failed()) {
			gr_set_color(255, 80, 64);
			centred(sw, 190, "Could not save this to the SD card.");
		}
	}
	/* hint strip: the longest hint that fits (the rule: never cut off); it follows left-hand mode */
	static const char *const main_r[] = { "D-Pad: choose   A: select   B or START: close", "A: select   B or START: close", "A: select  B: close" };
	static const char *const main_l[] = { "Y/B: choose   D-Right: select   D-Down or START: close", "D-Right: select   D-Down or START: close", "D-Right: select  D-Down: close" };
	static const char *const sub_r[] = { "D-Pad: choose   A: select   B: back", "A: select   B: back", "A: select  B: back" };
	static const char *const sub_l[] = { "Y/B: choose   D-Right: select   D-Down: back", "D-Right: select   D-Down: back", "D-Right: select" };
	hint_strip(sw, Pm_page == 0 ? (n3ds_lefty() ? main_l : main_r) : (n3ds_lefty() ? sub_l : sub_r), 3);
}

/* ---- message page (FS2-U1): a question the game asks in a pop-up, as big buttons.
   The game draws its pop-up on the game screen with answers a few pixels tall. While one with
   answers is up, the dashboard's screen (in flight: the cockpit panel's screen) shows the text and
   one full-width button per answer; a tap or D-Pad + A picks it and the pop-up takes it as if
   that answer had been clicked. B sends Esc, the game's own "cancel" for the pop-up. The focus
   starts on No or Cancel when there is one, else on the pop-up's own default (its first answer). */
extern int n3ds_popup_get(const char **text, const char **buttons, int max_buttons);    /* popup.cpp */
extern void n3ds_popup_choose(int choice);
#define PP_MAX 3
static int Pp_was = 0, Pp_n = 0, Pp_focus = 0, Pp_pressed = -1, Pp_over = 0, Pp_flash = 0, Pp_lx = 0, Pp_ly = 0;
static int Pp_flight = 0;
static char Pp_seen[48] = "";
#define PP_SW (Pp_flight ? 320 : DASH_W)

/* is a pop-up with answers up? Also notices a new one and puts the focus where it starts. */
static int pp_sync(const char **text, const char **buttons)
{
	const char *t = NULL, *bt[PP_MAX] = { NULL, NULL, NULL };
	int n = n3ds_popup_get(&t, bt, PP_MAX);
	if (n <= 0) { Pp_was = 0; Pp_pressed = -1; Pp_flash = 0; return 0; }
	if (!Pp_was || n != Pp_n || strncmp(t, Pp_seen, sizeof(Pp_seen) - 1)) {
		Pp_was = 1; Pp_n = n;
		snprintf(Pp_seen, sizeof(Pp_seen), "%s", t);
		Pp_focus = 0; Pp_pressed = -1; Pp_flash = 0; Pp_over = 0;
		for (int i = 0; i < n; i++)
			if (!stricmp(bt[i], "No") || !stricmp(bt[i], "Cancel")) { Pp_focus = i; break; }
	}
	if (text) *text = t;
	if (buttons) for (int i = 0; i < PP_MAX; i++) buttons[i] = bt[i];
	return n;
}

int n3ds_popup_page_active(void) { return pp_sync(NULL, NULL) > 0; }

static void pp_rect(int i, int *x, int *y, int *w, int *h)
{
	*x = 6; *w = PP_SW - 12; *h = PM_BH;
	*y = 220 - Pp_n * PM_BH - (Pp_n - 1) * PM_GAP + i * (PM_BH + PM_GAP);
}

static int pp_at(int tx, int ty)
{
	for (int i = 0; i < Pp_n; i++) {
		int x, y, w, h; pp_rect(i, &x, &y, &w, &h);
		if (tx >= x && tx < x + w && ty >= y && ty < y + h) return i;
	}
	return -1;
}

/* one call per input frame; returns 1 while the page is up (it then owns the buttons and the touch) */
int n3ds_popup_page_input(int in_flight, int dy, int a, int b, int touch_held, int tx, int ty, int touch_pressed)
{
	if (pp_sync(NULL, NULL) <= 0) return 0;
	Pp_flight = in_flight ? 1 : 0;
	if (Pp_flash > 0) {                          /* finish an A press; new presses are dropped */
		if (--Pp_flash == 0) n3ds_popup_choose(Pp_focus);
		return 1;
	}
	if (touch_pressed) {
		Pp_pressed = pp_at(tx, ty);
		Pp_lx = tx; Pp_ly = ty; Pp_over = Pp_pressed >= 0;
		if (Pp_pressed >= 0) { Pp_focus = Pp_pressed; n3ds_click(); }
		return 1;
	}
	if (touch_held) {                            /* sliding off the button un-presses it */
		Pp_lx = tx; Pp_ly = ty;
		Pp_over = Pp_pressed >= 0 && pp_at(tx, ty) == Pp_pressed;
		return 1;
	}
	if (Pp_pressed >= 0) {                       /* act on release; released outside = cancelled */
		int i = Pp_pressed; Pp_pressed = -1;
		if (Pp_over && pp_at(Pp_lx, Pp_ly) == i) n3ds_popup_choose(i);
		Pp_over = 0;
		return 1;
	}
	if (b) { n3ds_click(); n3ds_tap_key(KEY_ESC); return 1; }
	if (dy) Pp_focus = (Pp_focus + dy + Pp_n) % Pp_n;
	if (a) { n3ds_click(); Pp_flash = 6; }
	return 1;
}

/* word-wrap text (it may hold line breaks) into lines of at most maxw pixels; returns the count */
static int pp_wrap(const char *text, int maxw, char out[][128], int maxlines)
{
	int n = 0;
	const char *p = text;
	while (*p && n < maxlines) {
		while (*p == ' ') p++;
		if (*p == '\n') { p++; continue; }
		if (!*p) break;
		char line[128];
		int len = 0, last_space = -1;
		while (p[len] && p[len] != '\n' && len < 126) {
			memcpy(line, p, len + 1); line[len + 1] = 0;
			int w, h;
			gr_get_string_size(&w, &h, line);
			if (w > maxw) break;
			if (p[len] == ' ') last_space = len;
			len++;
		}
		int take = len;
		if (p[len] && p[len] != '\n' && last_space > 0) take = last_space;
		if (take <= 0) take = 1;
		memcpy(out[n], p, take); out[n][take] = 0;
		n++;
		p += take;
	}
	return n;
}

static void pp_draw(void)
{
	const char *text = "", *bt[PP_MAX];
	if (pp_sync(&text, bt) <= 0) return;
	const int sw = PP_SW;
	gr_set_color(8, 12, 40);                     /* navy body under the header bar */
	n3ds_rect(0, 22, sw, DASH_H - 22);
	gr_set_font(FONT1);
	int x, y0, w, h;
	pp_rect(0, &x, &y0, &w, &h);
	/* the text: whole lines only, above the buttons. If it is longer than the room, the last
	   line says where the rest is (the pop-up itself, on the game screen). */
	static char lines[24][128];
	int n = pp_wrap(text, sw - 12, lines, 24);
	int room = (y0 - 6 - 30) / 12;
	int y = 30;
	if (n <= room) y += ((room - n) * 12) / 2;   /* short text sits in the middle of its space */
	gr_set_color(240, 240, 240);
	for (int i = 0; i < n && i < room; i++, y += 12) {
		if (n > room && i == room - 1) {
			gr_set_color(150, 150, 165);
			centred(sw, y, "(the rest is on the game screen)");
			break;
		}
		centred(sw, y, lines[i]);
	}
	for (int i = 0; i < Pp_n; i++) {
		pp_rect(i, &x, &y, &w, &h);
		int pressed = (i == Pp_pressed && Pp_over) || (i == Pp_focus && Pp_flash > 0);
		house_button(x, y, w, h, bt[i], pressed, i == Pp_focus && Pp_pressed < 0);
	}
	static const char *const hr[] = { "D-Pad: choose   A: select   B: cancel", "A: select   B: cancel", "A: select" };
	static const char *const hl[] = { "Y/B: choose   D-Right: select   D-Down: cancel", "D-Right: select   D-Down: cancel", "D-Right: select" };
	hint_strip(sw, n3ds_lefty() ? hl : hr, 3);
}

/* the header bar of the flight pages (START menu, message page) on the cockpit panel's screen */
static void flight_header(const char *right)
{
	gr_set_clip(0, 0, 320, DASH_H, false);
	gr_set_color(8, 12, 40);
	n3ds_rect(0, 0, 320, DASH_H);
	gr_set_color(40, 56, 120);
	n3ds_rect(0, 0, 320, 22);
	gr_set_font(FONT1);
	gr_set_color(255, 204, 64);
	gr_string(8, 7, "FREESPACE 2", false);
	gr_set_color(150, 150, 165);
	int w, h;
	gr_get_string_size(&w, &h, right);
	gr_string(320 - 8 - w, 7, right, false);
}

/* the message page on the cockpit panel's screen in flight */
void n3ds_popup_draw_flight(void)
{
	Pp_flight = 1;
	flight_header("MESSAGE");
	pp_draw();
	gr_reset_clip();
}

void n3ds_dash_draw(void)
{
	Dash_frame++;
	dash_layout();

	int state = gameseq_get_state();
	const screen_help *h = NULL;
	for (int i = 0; i < NUM_HELP; i++)
		if (Help[i].state == state) { h = &Help[i]; break; }

	gr_set_clip(0, 0, DASH_W, DASH_H, false);
	gr_set_color(8, 12, 40);                  /* house palette: navy */
	n3ds_rect(0, 0, DASH_W, DASH_H);

	char buf[160];
	int w, hh;

	/* title bar: big font if it fits next to the clock, else the small one */
	gr_set_color(40, 56, 120);                /* header bar */
	n3ds_rect(0, 0, DASH_W, 22);
	gr_set_font(FONT1);
	time_t now = time(NULL);
	struct tm *lt = localtime(&now);
	int clock_w = 0;
	if (lt) {
		sprintf(buf, "%d:%02d", lt->tm_hour, lt->tm_min);
		gr_get_string_size(&clock_w, &hh, buf);
		gr_set_color(180, 180, 180);
		gr_string(DASH_W - 6 - clock_w, 7, buf, false);
	}
	const char *title = h ? h->title : "FREESPACE 2";
	gr_set_font(FONT2);
	gr_get_string_size(&w, &hh, title);
	if (w > DASH_W - 20 - clock_w) gr_set_font(FONT1);
	gr_set_color(255, 204, 64);
	gr_string(8, 4, title, false);
	gr_set_font(FONT1);

	/* pilot and campaign: label column sized to the widest label */
	int lw = 0;
	{
		const char *labels[] = { "PILOT", "CAMPAIGN", "NEXT", "POINTER" };
		for (int i = 0; i < 4; i++) {
			gr_get_string_size(&w, &hh, labels[i]);
			if (w > lw) lw = w;
		}
	}
	int vx = 6 + lw + 8;
	Wrap_bottom = (Dash_nact ? ACT_Y : BTN_Y) - 2;
	int y = 26;
	if (Player && Player->callsign[0]) {
		int r = Player->stats.rank;
		if (r < 0) r = 0;
		if (r >= NUM_RANKS) r = NUM_RANKS - 1;
		label(6, y, "PILOT");
		gr_set_color(235, 235, 235);
		sprintf(buf, "%s, %s - %d kills", Player->callsign, Ranks[r].name, Player->stats.kill_count_ok);
		gr_get_string_size(&w, &hh, buf);
		if (w > DASH_W - vx - 6) sprintf(buf, "%s, %s", Player->callsign, Ranks[r].name);
		y = wrapped(vx, y, buf, 12);

		if (Campaign.num_missions > 0) {
			label(6, y, "CAMPAIGN");
			gr_set_color(235, 235, 235);
			y = wrapped(vx, y, Campaign.name, 12);
			char next[40];
			int n = Campaign.next_mission;
			if (n >= 0 && n < Campaign.num_missions)
				mission_label(next, sizeof(next), Campaign.missions[n].name);
			else
				strcpy(next, "-");
			label(6, y, "NEXT");
			gr_set_color(235, 235, 235);
			sprintf(buf, "%s (%d of %d flown)", next, Campaign.num_missions_completed, Campaign.num_missions);
			y = wrapped(vx, y, buf, 12);
		}
	} else {
		value(6, y, "No pilot selected yet.");
		y += 12;
	}

	y += 1;
	hline(y);
	y += 4;

	/* what the pointer is over (screens that name their regions, like the main hall) */
	label(6, y, "POINTER");
	if (Dash_frame - Hover_frame <= 6 && Hover_text[0]) {
		gr_set_color(255, 255, 160);
		y = wrapped(vx, y, Hover_text, 12);
	} else {
		gr_set_color(110, 110, 110);
		gr_string(vx, y, "-", false);
		y += 12;
	}
	if (popup_active() && !n3ds_popup_page_active()) {
		/* a pop-up that takes typing (the answers of every other one are on the message page) */
		gr_set_color(255, 160, 120);
		y = wrapped(vx, y, Dash_nav ? "Message - answer it below." : "Message - answer it on top.", 12);
	}

	y += 1;
	hline(y);
	y += 4;

	/* help for this screen, most important line first. A line that would not fit
	   completely is left out instead of being cut off. */
	if (h) {
		gr_set_color(210, 210, 210);
		for (int i = 0; i < 6 && h->lines[i]; i++) {
			if (y + 12 * wrapped_lines(6, h->lines[i]) > Wrap_bottom) break;
			y = wrapped(6, y, h->lines[i], 12);
		}
	}

	draw_buttons();

	/* footer: one grey line of screen controls, then the gold hint strip (y 224, 16 px) */
	Wrap_bottom = DASH_H;
	/* swapped: the menu is on the touchscreen (FS2-U2: L / R zoom it, the sticks move the view) */
	const char *grey = Dash_nav ? "Stylus: mouse   Sticks: move the view   L / R: zoom" : "Pad / C-Stick / slide here: pointer";
	const char *gold;
	if (Dash_nav) gold = n3ds_lefty() ? "ABXY: pick   D-Rt: press   START: menu" : "D-Pad: pick   A: press   START: menu";
	else gold = n3ds_lefty() ? "D-Rt/tap: click  D-Lt: Enter  START: menu" : "A/tap: click  X: Enter  B: back  START: menu";
	gr_get_string_size(&w, &hh, gold);
	if (w > DASH_W - 8) {            /* the shorter hint follows left-hand mode too */
		if (Dash_nav) gold = n3ds_lefty() ? "D-Rt: press   START: menu" : "A: press   START: menu";
		else gold = n3ds_lefty() ? "D-Rt/tap: click   START: menu" : "A/tap: click   START: menu";
	}
	gr_set_color(150, 150, 165);
	gr_get_string_size(&w, &hh, grey);
	gr_string((DASH_W - w) / 2, 211, grey, false);
	gr_set_color(4, 6, 20);
	n3ds_rect(0, 224, DASH_W, 16);
	gr_get_string_size(&w, &hh, gold);
	gr_set_color(255, 204, 64);
	gr_string((DASH_W - w) / 2, 224 + (16 - hh) / 2, gold, false);

	if (Pm_open) pm_draw();
	else if (n3ds_popup_page_active()) { Pp_flight = 0; pp_draw(); }

	gr_reset_clip();
}

/* FS-08: the same menu drawn on the cockpit panel's screen in flight (header bar and all) */
void n3ds_portmenu_draw_flight(void)
{
	flight_header("PAUSED");
	pm_draw();
	gr_reset_clip();
}
