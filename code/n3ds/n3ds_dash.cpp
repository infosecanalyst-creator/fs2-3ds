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
		"Pick your pilot in the list, then Select (lower right).",
		"New pilot: Create, then type a callsign on the keyboard.",
		"Pilot files live in sdmc:/3ds/freespace2/.fs2_open", NULL } },
	{ GS_STATE_MAIN_MENU, "MAIN HALL", {
		"Point at a room - its name shows on this screen.",
		"Press A to go in.",
		"Ready Room: campaign   Tech Room: ship and weapon data",
		"Barracks: pilots and medals   Options: detail and sound",
		"Lower detail in Options if a battle feels slow.", NULL } },
	{ GS_STATE_CMD_BRIEF, "COMMAND BRIEFING", {
		"Orders from command for the coming operation.",
		"Arrows step through the pages.",
		"Continue (lower right) goes on to the mission briefing.", NULL } },
	{ GS_STATE_BRIEFING, "MISSION BRIEFING", {
		"Arrows step through the briefing stages.",
		"Ship Selection / Weapon Loadout (top left) change your fit.",
		"Commit (lower right) launches the mission.", NULL } },
	{ GS_STATE_SHIP_SELECT, "SHIP SELECTION", {
		"Drag a ship from the pool onto a wing slot.",
		"Commit (lower right) launches the mission.", NULL } },
	{ GS_STATE_WEAPON_SELECT, "WEAPON LOADOUT", {
		"Drag weapons from the list onto your ship's banks.",
		"Commit (lower right) launches the mission.", NULL } },
	{ GS_STATE_DEBRIEF, "DEBRIEFING", {
		"How the mission went, and what command thinks of it.",
		"Accept (lower right) records the result.", NULL } },
	{ GS_STATE_OPTIONS_MENU, "OPTIONS", {
		"Detail tab: Medium is the New 3DS default.",
		"Lower it for smoother battles, raise it for looks.",
		"Controls are fixed on 3DS - see the manual.", NULL } },
	{ GS_STATE_CONTROL_CONFIG, "CONTROLS", {
		"The 3DS buttons are mapped for you.",
		"SELECT + START opens the manual with the full list.", NULL } },
	{ GS_STATE_HUD_CONFIG, "HUD CONFIG", {
		"Tap a gauge to toggle it or change its colour.", NULL } },
	{ GS_STATE_TECH_MENU, "TECH ROOM", {
		"Ship and weapon database, intelligence, cutscenes.", NULL } },
	{ GS_STATE_BARRACKS_MENU, "BARRACKS", {
		"Pilots, squadron logos and service records.",
		"Make Active switches pilot.", NULL } },
	{ GS_STATE_CAMPAIGN_ROOM, "CAMPAIGN ROOM", {
		"Choose or restart a campaign.", NULL } },
	{ GS_STATE_SIMULATOR_ROOM, "SIMULATOR", {
		"Replay campaign missions and single missions.", NULL } },
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
		"No time for a briefing - Continue to launch.", NULL } },
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
#define BTN_Y 178
#define BTN_H 26
static const dash_button Dash_buttons[] = {
	{   2, BTN_Y, 62, BTN_H, "BACK" },
	{  66, BTN_Y, 62, BTN_H, "ENTER" },
	{ 130, BTN_Y, 62, BTN_H, "ZOOM -" },
	{ 194, BTN_Y, 62, BTN_H, "ZOOM +" },
	{ 258, BTN_Y, 60, BTN_H, "MANUAL" },
};
#define NUM_DASH_BUTTONS ((int)(sizeof(Dash_buttons) / sizeof(Dash_buttons[0])))
static int Dash_pressed = -1;

extern "C" void n3ds_click(void);
extern "C" void n3ds_menu_zoom(int delta);
extern "C" void n3ds_tap_key(int key);
#include "io/key.h"
#include "n3ds/n3ds_manual.h"

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
		switch (Dash_pressed) {
			case 0: n3ds_tap_key(KEY_ESC); break;
			case 1: n3ds_tap_key(KEY_ENTER); break;
			case 2: n3ds_menu_zoom(-1); break;
			case 3: n3ds_menu_zoom(+1); break;
			case 4: n3ds_manual_toggle(); break;
		}
		Dash_pressed = -1;
	}
}

static void draw_buttons(void)
{
	for (int i = 0; i < NUM_DASH_BUTTONS; i++) {
		const dash_button &b = Dash_buttons[i];
		int pressed = (i == Dash_pressed);
		int off = pressed ? 1 : 0;       /* pressed buttons sink by a pixel and light up */
		gr_set_color(pressed ? 90 : 40, pressed ? 140 : 70, pressed ? 200 : 110);
		gr_rect(b.x, b.y + off, b.w, b.h, false);
		gr_set_color(pressed ? 160 : 70, pressed ? 200 : 110, pressed ? 250 : 160);
		gr_rect(b.x, b.y + off, b.w, 1, false);
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

	/* pilot and campaign */
	Wrap_bottom = BTN_Y - 4;
	int y = 27;
	if (Player && Player->callsign[0]) {
		int r = Player->stats.rank;
		if (r < 0) r = 0;
		if (r >= NUM_RANKS) r = NUM_RANKS - 1;
		label(6, y, "PILOT");
		gr_set_color(235, 235, 235);
		sprintf(buf, "%s, %s - %d kills, %u missions", Player->callsign, Ranks[r].name,
			Player->stats.kill_count_ok, Player->stats.missions_flown);
		y = wrapped(62, y, buf, 12);

		if (Campaign.num_missions > 0) {
			label(6, y, "CAMPAIGN");
			gr_set_color(235, 235, 235);
			y = wrapped(62, y, Campaign.name, 12);
			char next[40];
			int n = Campaign.next_mission;
			if (n >= 0 && n < Campaign.num_missions)
				mission_label(next, sizeof(next), Campaign.missions[n].name);
			else
				strcpy(next, "-");
			label(6, y, "NEXT");
			gr_set_color(235, 235, 235);
			sprintf(buf, "%s (%d of %d flown)", next, Campaign.num_missions_completed, Campaign.num_missions);
			y = wrapped(62, y, buf, 12);
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
	if (Dash_frame - Hover_frame <= 2 && Hover_text[0]) {
		gr_set_color(255, 255, 160);
		y = wrapped(62, y, Hover_text, 12);
	} else {
		gr_set_color(110, 110, 110);
		gr_string(62, y, "-", false);
		y += 12;
	}
	if (popup_active()) {
		gr_set_color(255, 160, 120);
		y = wrapped(62, y, "A message is waiting for an answer.", 12);
	}

	y += 1;
	hline(y);
	y += 4;

	/* help for this screen */
	if (h) {
		gr_set_color(210, 210, 210);
		for (int i = 0; i < 6 && h->lines[i]; i++)
			y = wrapped(6, y, h->lines[i], 12);
	}

	draw_buttons();

	/* controls footer */
	gr_set_color(24, 44, 74);
	gr_rect(0, DASH_H - 30, DASH_W, 30, false);
	gr_set_color(200, 200, 200);
	Wrap_bottom = DASH_H;
	wrapped(6, DASH_H - 28, "Pad / C-Stick: pointer   A: click   X: Enter", 13);
	wrapped(6, DASH_H - 15, "B: back   L / R: zoom   SELECT+START: manual", 13);

	gr_reset_clip();
}
