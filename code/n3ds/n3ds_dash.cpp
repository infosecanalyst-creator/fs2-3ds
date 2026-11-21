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

#define DASH_W 320
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
		"Point at a room, then press A to go in.",
		"The Ready Room (middle door) starts the campaign.",
		"Options: lower detail if battles feel slow.", NULL } },
	{ GS_STATE_CMD_BRIEF, "COMMAND BRIEFING", {
		"Continue (lower right) goes to the briefing.",
		"Arrows step through the pages.", NULL } },
	{ GS_STATE_BRIEFING, "MISSION BRIEFING", {
		"Commit (lower right) launches the mission.",
		"Arrows step through the briefing stages.",
		"Ship and weapon tabs are at the top left.", NULL } },
	{ GS_STATE_SHIP_SELECT, "SHIP SELECTION", {
		"Commit (lower right) launches the mission.",
		"Drag a ship from the pool onto a wing slot.", NULL } },
	{ GS_STATE_WEAPON_SELECT, "WEAPON LOADOUT", {
		"Commit (lower right) launches the mission.",
		"Drag weapons onto your ship's banks.", NULL } },
	{ GS_STATE_DEBRIEF, "DEBRIEFING", {
		"Accept (lower right) records the result.", NULL } },
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
		"No briefing - Continue to launch.", NULL } },
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
static const dash_button Dash_buttons[] = {
	/* rule 11: 74 wide, 4 px apart, 6 px from the edges (x 6..314) */
	{   6, BTN_Y, 74, BTN_H, "BACK" },
	{  84, BTN_Y, 74, BTN_H, "ENTER" },
	{ 162, BTN_Y, 74, BTN_H, "ZOOM" },
	{ 240, BTN_Y, 74, BTN_H, "MANUAL" },
};
#define NUM_DASH_BUTTONS ((int)(sizeof(Dash_buttons) / sizeof(Dash_buttons[0])))
static int Dash_pressed = -1;
static int Dash_nav = 0;           /* the dashboard is on the top screen: highlight shown */
static int Dash_focus = 0;         /* highlighted button */
static int Dash_flash = 0;         /* frames left of an A press's animation */

extern "C" void n3ds_click(void);
extern "C" void n3ds_menu_zoom(int delta);
extern "C" void n3ds_tap_key(int key);
#include "io/key.h"
#include "n3ds/n3ds_manual.h"
#ifndef PORT_VERSION
#define PORT_VERSION "unknown"
#endif

static void dash_action(int i)
{
	switch (i) {
		case 0: n3ds_tap_key(KEY_ESC); break;
		case 1: n3ds_tap_key(KEY_ENTER); break;
		case 2: n3ds_menu_zoom(0); break;      /* 0 = cycle */
		case 3: n3ds_manual_toggle(); break;
	}
}

int n3ds_dash_button_at(int x, int y)
{
	for (int i = 0; i < NUM_DASH_BUTTONS; i++) {
		const dash_button &b = Dash_buttons[i];
		if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h)
			return i;
	}
	return -1;
}

void n3ds_dash_set_nav(int on) { Dash_nav = on; }

/* The buttons are one row: Left / Right move the highlight, A presses it - the button sinks
   and lights for a few frames (as when tapped) and then acts. A press during that time is
   ignored, never saved for later. */
void n3ds_dash_nav(int dx, int dy, int press)
{
	(void)dy;
	if (n3ds_manual_is_open()) { Dash_flash = 0; return; }   /* a press is dropped, not saved */
	if (Dash_flash > 0) {
		if (--Dash_flash == 0) dash_action(Dash_focus);
		return;
	}
	if (!Dash_nav) return;
	if (dx < 0 && Dash_focus > 0) Dash_focus--;
	if (dx > 0 && Dash_focus < NUM_DASH_BUTTONS - 1) Dash_focus++;
	if (press) {
		n3ds_click();
		Dash_flash = 6;
	}
}

void n3ds_dash_touch(int held, int x, int y, int pressed)
{
	if (pressed) {
		Dash_pressed = -1;
		for (int i = 0; i < NUM_DASH_BUTTONS; i++) {
			const dash_button &b = Dash_buttons[i];
			if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) {
				Dash_pressed = i;
				n3ds_click();
				break;
			}
		}
		return;
	}
	if (!held && Dash_pressed >= 0) {
		/* act on release, like a real button */
		dash_action(Dash_pressed);
		Dash_pressed = -1;
	}
}

static void draw_buttons(void)
{
	/* house style: face 40,56,120, edge 120,150,230, shadow line, pressed 220,180,60, gold focus ring */
	for (int i = 0; i < NUM_DASH_BUTTONS; i++) {
		const dash_button &b = Dash_buttons[i];
		int focused = Dash_nav && i == Dash_focus;
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

/* ---- START port menu (house style: claude/3ds-look-and-feel.md) */
int n3ds_exit_requested = 0;
static int Pm_open = 0;
static int Pm_page = 0;            /* 0 main, 1 exit confirm */
static int Pm_focus = 0;
static int Pm_pressed = -1;        /* button under the stylus */
static int Pm_flash = 0;           /* frames of an A press's animation */
#define PM_X 40
#define PM_W 240
#define PM_BH 30
#define PM_GAP 6
static const char *Pm_main[] = { "RESUME", "MANUAL", "EXIT GAME" };
static const char *Pm_confirm[] = { "YES", "NO" };

int n3ds_portmenu_is_open(void) { return Pm_open; }
void n3ds_portmenu_open(void) { Pm_open = 1; Pm_page = 0; Pm_focus = 0; Pm_pressed = -1; Pm_flash = 0; }
void n3ds_portmenu_close(void) { Pm_open = 0; Pm_pressed = -1; Pm_flash = 0; }

static int pm_count(void) { return Pm_page == 0 ? 3 : 2; }

/* button rectangles: main page stacked in the middle; confirm page side by side */
static void pm_rect(int i, int *x, int *y, int *w, int *h)
{
	if (Pm_page == 0) {
		*x = PM_X; *w = PM_W; *h = PM_BH;
		*y = 60 + i * (PM_BH + PM_GAP);
	} else {
		*w = 114; *h = PM_BH; *y = 130;
		*x = PM_X + i * (114 + 12);
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
		else { Pm_page = 1; Pm_focus = 1; }       /* focus starts on NO */
	} else {
		if (i == 0) { n3ds_portmenu_close(); n3ds_exit_requested = 1; }
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
		if (Pm_pressed >= 0) { Pm_focus = Pm_pressed; n3ds_click(); }
		return;
	}
	if (!touch_held && Pm_pressed >= 0) {        /* act on release, like a real button */
		int i = Pm_pressed; Pm_pressed = -1;
		if (pm_at(tx, ty) == i || (tx == 0 && ty == 0)) pm_act(i);
		return;
	}
	if (touch_held) return;
	if (b || start) {
		n3ds_click();
		if (Pm_page == 1) { Pm_page = 0; Pm_focus = 2; } else n3ds_portmenu_close();
		return;
	}
	int n = pm_count();
	if (Pm_page == 0) { if (dy) Pm_focus = (Pm_focus + dy + n) % n; }
	else if (dy) Pm_focus = 1 - Pm_focus;
	if (a) { n3ds_click(); Pm_flash = 6; }
}

static void pm_button(int i, const char *text)
{
	int x, y, w, h; pm_rect(i, &x, &y, &w, &h);
	int pressed = (i == Pm_pressed) || (i == Pm_focus && Pm_flash > 0);
	int off = pressed ? 1 : 0;
	if (pressed) gr_set_color(220, 180, 60); else gr_set_color(40, 56, 120);
	n3ds_rect(x, y + off, w, h);
	gr_set_color(120, 150, 230);                 /* edge */
	n3ds_rect(x, y + off, w, 1); n3ds_rect(x, y + off, 1, h); n3ds_rect(x + w - 1, y + off, 1, h);
	gr_set_color(20, 30, 70);                    /* shadow */
	n3ds_rect(x, y + off + h - 1, w, 1);
	if (i == Pm_focus && Pm_pressed < 0) {       /* gold focus ring */
		gr_set_color(255, 204, 64);
		n3ds_rect(x, y + off, w, 2); n3ds_rect(x, y + off + h - 2, w, 2);
		n3ds_rect(x, y + off, 2, h); n3ds_rect(x + w - 2, y + off, 2, h);
	}
	int tw, th; gr_get_string_size(&tw, &th, text);
	gr_set_color(240, 240, 240);
	gr_string(x + (w - tw) / 2, y + off + (h - th) / 2, text, false);
}

static void pm_draw(void)
{
	gr_set_color(8, 12, 40);                     /* navy body under the header bar */
	n3ds_rect(0, 22, DASH_W, DASH_H - 22);
	int w, h;
	gr_set_font(FONT1);
	if (Pm_page == 0) {
		const char *t = "MENU";
		gr_get_string_size(&w, &h, t);
		gr_set_color(255, 204, 64);
		gr_string((DASH_W - w) / 2, 36, t, false);
		for (int i = 0; i < 3; i++) pm_button(i, Pm_main[i]);
		const char *v = "Build " PORT_VERSION;   /* port version (git describe of fs2-3ds) */
		gr_get_string_size(&w, &h, v);
		gr_set_color(150, 150, 165);
		gr_string((DASH_W - w) / 2, 190, v, false);
	} else {
		const char *t1 = "EXIT FREESPACE 2?", *t2 = "Progress since your last save is lost.";
		gr_get_string_size(&w, &h, t1);
		gr_set_color(255, 204, 64);
		gr_string((DASH_W - w) / 2, 70, t1, false);
		gr_get_string_size(&w, &h, t2);
		gr_set_color(240, 240, 240);
		gr_string((DASH_W - w) / 2, 92, t2, false);
		for (int i = 0; i < 2; i++) pm_button(i, Pm_confirm[i]);
	}
	/* hint strip */
	gr_set_color(4, 6, 20);
	n3ds_rect(0, 224, DASH_W, 16);
	/* the longest hint that fits (the rule: never cut off) */
	const char *hints[] = { "D-Pad: choose   A: select   B: back", "D-Pad  A: select  B: back", "A: select  B: back" };
	const char *hint = hints[2];
	for (int i = 0; i < 3; i++) {
		gr_get_string_size(&w, &h, hints[i]);
		if (w <= DASH_W - 8) { hint = hints[i]; break; }
	}
	gr_get_string_size(&w, &h, hint);
	gr_set_color(255, 204, 64);
	gr_string((DASH_W - w) / 2, 224 + (16 - h) / 2, hint, false);
}

void n3ds_dash_draw(void)
{
	Dash_frame++;

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
	Wrap_bottom = BTN_Y - 2;
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
	if (popup_active()) {
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
	const char *grey = Dash_nav ? "Stylus: mouse on the touchscreen" : "Pad / C-Stick / slide here: pointer";
	const char *gold;
	if (Dash_nav) gold = n3ds_lefty() ? "ABXY: pick   D-Rt: press   START: menu" : "D-Pad: pick   A: press   START: menu";
	else gold = n3ds_lefty() ? "D-Rt/tap: click  D-Lt: Enter  START: menu" : "A/tap: click  X: Enter  B: back  START: menu";
	gr_get_string_size(&w, &hh, gold);
	if (w > DASH_W - 8) gold = Dash_nav ? "A: press   START: menu" : "A/tap: click   START: menu";
	gr_set_color(150, 150, 165);
	gr_get_string_size(&w, &hh, grey);
	gr_string((DASH_W - w) / 2, 211, grey, false);
	gr_set_color(4, 6, 20);
	n3ds_rect(0, 224, DASH_W, 16);
	gr_get_string_size(&w, &hh, gold);
	gr_set_color(255, 204, 64);
	gr_string((DASH_W - w) / 2, 224 + (16 - hh) / 2, gold, false);

	if (Pm_open) pm_draw();

	gr_reset_clip();
}
