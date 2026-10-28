/*
 * Built-in user manual for FreeSpace 2 on the New 3DS.
 * Open with SELECT + START at any time (pauses the game in flight).
 * L/R or D-pad left/right turn pages, B or SELECT + START closes.
 * Drawn 1:1 on the touchscreen so the text stays sharp.
 */
#include "n3ds/n3ds_manual.h"

#include "globalincs/pstypes.h"
#include "graphics/2d.h"
#include "graphics/font.h"

struct manual_page {
	const char *title;
	const char *lines[20];
};

static const manual_page Pages[] = {
	{ "WELCOME", {
		"FreeSpace 2 for the New 3DS (fs2_open 3.6.10).",
		"",
		"Top screen: the cockpit view and HUD.",
		"Touchscreen: your cockpit panel - target",
		"info, ship status, energy and quick orders.",
		"",
		"In menus the touchscreen shows the whole",
		"menu: tap it, or steer the pointer with",
		"the Circle Pad and press A to click. The",
		"top screen is a dashboard: pilot, campaign,",
		"what the pointer is over, and help.",
		"",
		"L / R turn pages. B closes this manual.",
		NULL } },
	{ "FLYING", {
		"Circle Pad   pitch and turn",
		"C-Stick      roll (left/right)",
		"ZR / ZL      throttle up / down (hold)",
		"A            afterburner (hold)",
		"R            fire primary (hold)",
		"L            fire secondary",
		"B            launch countermeasure",
		"Y            target next ship",
		"X            target ship in reticle",
		"D-Pad Up     target nearest hostile",
		"D-Pad Down   match target speed",
		"D-Pad Left   cycle primary weapons",
		"D-Pad Right  cycle secondary weapons",
		"START        pause / options menu",
		"SELECT (tap) comms menu (wingman orders)",
		NULL } },
	{ "SELECT COMBOS", {
		"Hold SELECT and press:",
		"",
		"Y            target next friendly",
		"X            target next subsystem",
		"A            equalize shields",
		"D-Pad Up     target closest attacker",
		"D-Pad Down   target next bomb/bomber",
		"D-Pad Left   toggle target view",
		"D-Pad Right  radar range",
		"START        open this manual",
		NULL } },
	{ "COCKPIT PANEL", {
		"Top strip: current target - name, class,",
		"hull strength and distance.",
		"",
		"HULL / SHLD / AFTB / WPN bars show your",
		"own ship. SPD is your speed, THR your",
		"throttle setting.",
		"",
		"ENERGY (S W E): tap the top half of a",
		"column to give that system more power,",
		"the bottom half to give it less.",
		"",
		"Bottom buttons: HOSTILE, NEXT, FRIEND,",
		"SUBSYS, ATTACKR target selection; MATCH",
		"speed; ORDERS (comms); EQ SHLD; BOMB;",
		"RADAR range.",
		NULL } },
	{ "MENUS", {
		"Tap with the stylus to click, or move",
		"the pointer with the Circle Pad and",
		"press A to click.",
		"X = Enter, B = Esc, D-Pad = arrow keys.",
		"",
		"Main hall: point at a room - its name",
		"shows on the top screen - then click.",
		"",
		"Pilot files, settings and saved games are",
		"kept in sdmc:/3ds/freespace2/.fs2_open",
		NULL } },
	{ "INSTALLING / DATA", {
		"Game data (from your own GOG or Steam",
		"copy) goes in sdmc:/3ds/freespace2/ :",
		"",
		"  root_fs2.vp   smarty_fs2.vp",
		"  sparky_fs2.vp stu_fs2.vp",
		"  tango1_fs2.vp tango2_fs2.vp",
		"  tango3_fs2.vp warble_fs2.vp",
		"  tangoA_fs2.vp tangoB_fs2.vp",
		"",
		"sparky_hi_fs2.vp is not needed.",
		"A log is written to fs2_3ds.log there.",
		NULL } },
};
#define NUM_PAGES ((int)(sizeof(Pages) / sizeof(Pages[0])))

static int Manual_open = 0;
static int Manual_page = 0;

int n3ds_manual_is_open(void) { return Manual_open; }
void n3ds_manual_toggle(void) { Manual_open = !Manual_open; }
void n3ds_manual_close(void) { Manual_open = 0; }

void n3ds_manual_page(int delta)
{
	Manual_page += delta;
	if (Manual_page < 0) Manual_page = 0;
	if (Manual_page >= NUM_PAGES) Manual_page = NUM_PAGES - 1;
}

/* draws into the 320x240 region at the top-left of the main target */
void n3ds_manual_draw(void)
{
	const manual_page &p = Pages[Manual_page];
	gr_set_clip(0, 0, 320, 240, false);
	gr_set_color(6, 10, 18);
	gr_rect(0, 0, 320, 240, false);
	gr_set_color(30, 50, 80);
	gr_rect(0, 0, 320, 18, false);
	gr_set_font(FONT1);

	char buf[64];
	gr_set_color(255, 220, 120);
	gr_string(6, 4, p.title, false);
	sprintf(buf, "%d / %d", Manual_page + 1, NUM_PAGES);
	int w, h;
	gr_get_string_size(&w, &h, buf);
	gr_set_color(180, 180, 180);
	gr_string(314 - w, 4, buf, false);

	gr_set_color(230, 230, 230);
	int y = 26;
	for (int i = 0; i < 20 && p.lines[i]; i++) {
		gr_string(6, y, p.lines[i], false);
		y += 13;
	}

	gr_set_color(140, 140, 140);
	gr_string(6, 226, "L/R: page   B: close", false);
	gr_reset_clip();
}
