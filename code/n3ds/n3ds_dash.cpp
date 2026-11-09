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
	gr_set_color(40, 70, 110);
	gr_rect(4, y, DASH_W - 8, 1, false);
}

static void label(int x, int y, const char *text)
{
	gr_set_color(120, 160, 210);
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
#define BTN_Y 184
#define BTN_H 26
static const dash_button Dash_buttons[] = {
	{   2, BTN_Y, 77, BTN_H, "BACK" },
	{  81, BTN_Y, 77, BTN_H, "ENTER" },
	{ 160, BTN_Y, 77, BTN_H, "ZOOM" },
	{ 239, BTN_Y, 79, BTN_H, "MANUAL" },
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
	for (int i = 0; i < NUM_DASH_BUTTONS; i++) {
		const dash_button &b = Dash_buttons[i];
		int focused = Dash_nav && i == Dash_focus;
		int pressed = (i == Dash_pressed) || (focused && Dash_flash > 0);
		int off = pressed ? 1 : 0;       /* pressed buttons sink by a pixel and light up */
		gr_set_color(pressed ? 90 : 40, pressed ? 140 : 70, pressed ? 200 : 110);
		gr_rect(b.x, b.y + off, b.w, b.h, false);
		gr_set_color(pressed ? 160 : 70, pressed ? 200 : 110, pressed ? 250 : 160);
		gr_rect(b.x, b.y + off, b.w, 1, false);
		if (focused) {
			/* the highlight: a gold ring just inside the button */
			gr_set_color(255, 220, 120);
			gr_rect(b.x, b.y + off, b.w, 2, false);
			gr_rect(b.x, b.y + off + b.h - 2, b.w, 2, false);
			gr_rect(b.x, b.y + off, 2, b.h, false);
			gr_rect(b.x + b.w - 2, b.y + off, 2, b.h, false);
		}
		int w, h;
		gr_get_string_size(&w, &h, b.label);
		gr_set_color(240, 240, 240);
		gr_string(b.x + (b.w - w) / 2, b.y + off + (b.h - h) / 2, b.label, false);
	}
}

void n3ds_dash_draw(void)
{
	Dash_frame++;

	int state = gameseq_get_state();
	const screen_help *h = NULL;
	for (int i = 0; i < NUM_HELP; i++)
		if (Help[i].state == state) { h = &Help[i]; break; }

	gr_set_clip(0, 0, DASH_W, DASH_H, false);
	gr_set_color(6, 10, 18);
	gr_rect(0, 0, DASH_W, DASH_H, false);

	char buf[160];
	int w, hh;

	/* title bar: big font if it fits next to the clock, else the small one */
	gr_set_color(24, 44, 74);
	gr_rect(0, 0, DASH_W, 22, false);
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
	gr_set_color(255, 220, 120);
	gr_string(6, 4, title, false);
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

	/* controls footer */
	gr_set_color(24, 44, 74);
	gr_rect(0, DASH_H - 29, DASH_W, 29, false);
	gr_set_color(200, 200, 200);
	Wrap_bottom = DASH_H;
	if (Dash_nav) {
		/* on the top screen (ZL): the menu is on the touchscreen */
		gr_string(6, DASH_H - 27, "Stylus: mouse on the touchscreen", false);
		gr_string(6, DASH_H - 14, n3ds_lefty() ? "ABXY: pick a button   D-Rt: press it" : "D-Pad: pick a button   A: press it", false);
	} else {
		gr_string(6, DASH_H - 27, "Pad / C-Stick / slide here: pointer", false);
		gr_string(6, DASH_H - 14, n3ds_lefty() ? "D-Rt or tap: click  D-Lt: Enter  D-Dn: back" : "A or tap: click  X: Enter  B: back", false);
	}

	gr_reset_clip();
}
