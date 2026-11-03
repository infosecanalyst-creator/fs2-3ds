/*
 * Built-in user manual for FreeSpace 2 on the New 3DS.
 *
 * Same controls as every other 3DS port:
 *   Circle Pad / C-Stick   scroll the text (hold to keep scrolling)
 *   D-Pad                  move between the section buttons (touchscreen)
 *   A                      open the highlighted section
 *   L / R                  previous / next section
 *   tap                    open a section
 *   B or START             close
 * The text is on the top screen, the section buttons on the touchscreen.
 * Open with SELECT + START (pauses the game in flight) or the MANUAL button.
 */
#include "n3ds/n3ds_manual.h"

#include <string.h>

#include "globalincs/pstypes.h"
#include "graphics/2d.h"
#include "graphics/font.h"

extern "C" void n3ds_click(void);

struct manual_section {
	const char *title;
	const char *lines[40];
};

static const manual_section Sections[] = {
	{ "WELCOME", {
		"FreeSpace 2 for the New 3DS (fs2_open 3.6.10).",
		"",
		"IN FLIGHT",
		"Top screen: the cockpit view and HUD.",
		"Touchscreen: your cockpit panel - target info,",
		"ship status, energy and quick orders.",
		"",
		"IN MENUS",
		"Top screen: the menu. Move the pointer with the",
		"Circle Pad or C-Stick and press A to click. The",
		"view follows the pointer; L / R zoom out and in.",
		"Touchscreen: dashboard - pilot, campaign, the",
		"name of what the pointer is over, help for this",
		"screen, and BACK, ENTER, ZOOM and MANUAL",
		"buttons.",
		NULL } },
	{ "FLYING", {
		"Circle Pad    pitch and turn",
		"C-Stick       roll",
		"ZR / ZL       throttle up / down (hold)",
		"A             afterburner (hold)",
		"R             fire primary (hold)",
		"L             fire secondary",
		"B             launch countermeasure",
		"Y             target next ship",
		"X             target ship in reticle",
		"D-Pad Up      target nearest hostile",
		"D-Pad Down    match target speed",
		"D-Pad Left    cycle primary weapons",
		"D-Pad Right   cycle secondary weapons",
		"START         pause / options menu",
		"SELECT (tap)  comms menu (wingman orders)",
		NULL } },
	{ "SELECT COMBOS", {
		"Hold SELECT and press:",
		"",
		"Y             target next friendly",
		"X             target next subsystem",
		"A             equalize shields",
		"D-Pad Up      target closest attacker",
		"D-Pad Down    target next bomb / bomber",
		"D-Pad Left    toggle target view",
		"D-Pad Right   radar range",
		"START         open this manual",
		NULL } },
	{ "COCKPIT PANEL", {
		"Top strip: current target - name, class, hull",
		"strength and distance.",
		"",
		"HULL, SHLD, AFTB and WPN bars show your ship.",
		"SPD is your speed, THR your throttle setting.",
		"An M beside SPD: matching the target's speed.",
		"PRI / SEC: selected weapons and missiles left.",
		"The box below shows training text, directives",
		"and the latest radio messages.",
		"",
		"ENERGY (S W E): tap the top half of a column to",
		"give that system more power, the bottom half to",
		"give it less.",
		"",
		"Buttons: HOSTILE, NEXT, FRIEND, SUBSYS and",
		"ATTACK pick targets; MATCH matches speed;",
		"ORDERS opens comms; SHIELDS evens your shields;",
		"BOMB targets bombs; RADAR changes radar range.",
		NULL } },
	{ "MENUS", {
		"Circle Pad / C-Stick  move the pointer",
		"A                     click",
		"X                     Enter (accept)",
		"B                     back (Esc)",
		"D-Pad                 arrow keys",
		"L / R                 zoom the view out / in",
		"ZOOM button           cycle the zoom levels",
		"",
		"The top-screen view scrolls when the pointer",
		"nears its edge. Zoom out (L) to see the whole",
		"screen, zoom in (R) to read small text.",
		"",
		"Main hall: point at a room - its name shows on",
		"the touchscreen - then press A to go in.",
		"The Ready Room (middle door) starts the campaign.",
		"",
		"Pilot files, settings and saves are kept in",
		"sdmc:/3ds/freespace2/.fs2_open",
		NULL } },
	{ "DATA FILES", {
		"Game data (from your own GOG or Steam copy)",
		"goes in sdmc:/3ds/freespace2/ :",
		"",
		"  root_fs2.vp     smarty_fs2.vp",
		"  sparky_fs2.vp   stu_fs2.vp",
		"  tango1_fs2.vp   tango2_fs2.vp",
		"  tango3_fs2.vp   warble_fs2.vp",
		"  tangoa_fs2.vp   tangob_fs2.vp",
		"",
		"sparky_hi_fs2.vp is not needed.",
		"A log is written to fs2_3ds.log in that folder.",
		NULL } },
};
#define NUM_SECTIONS ((int)(sizeof(Sections) / sizeof(Sections[0])))

static int Manual_open = 0;
static int Manual_section = 0;     /* section shown */
static int Manual_focus = 0;       /* section button highlighted by the D-Pad */
static float Manual_scroll = 0;    /* pixels */
static int Manual_max_scroll = 0;
static int Manual_pressed = -1;    /* touched button, for the press animation */

#define LINE_H 13
#define TEXT_TOP 24
#define TEXT_BOTTOM 224

int n3ds_manual_is_open(void) { return Manual_open; }
void n3ds_manual_toggle(void) { Manual_open = !Manual_open; Manual_focus = Manual_section; Manual_pressed = -1; }
void n3ds_manual_close(void) { Manual_open = 0; }

static void open_section(int s)
{
	if (s < 0) s = 0;
	if (s >= NUM_SECTIONS) s = NUM_SECTIONS - 1;
	if (s != Manual_section) Manual_scroll = 0;
	Manual_section = s;
	Manual_focus = s;
}

void n3ds_manual_page(int delta) { open_section(Manual_section + delta); }

/* section buttons: 2 columns x 3 rows on the touchscreen */
static void button_rect(int i, int *x, int *y, int *w, int *h)
{
	*w = 152; *h = 44;
	*x = 6 + (i % 2) * 156;
	*y = 30 + (i / 2) * 58;
}

void n3ds_manual_scroll(int axis)
{
	/* stick deflection -> speed; holding keeps scrolling. Push up = scroll up. */
	if (axis > 18 || axis < -18) {
		float t = ((axis < 0 ? -axis : axis) - 18) / 130.0f;
		if (t > 1.0f) t = 1.0f;
		float v = 1.0f + 9.0f * t * t;
		Manual_scroll += (axis > 0) ? -v : v;
	}
	if (Manual_scroll < 0) Manual_scroll = 0;
	if (Manual_scroll > Manual_max_scroll) Manual_scroll = (float)Manual_max_scroll;
}

void n3ds_manual_dpad(int dx, int dy)
{
	if (dx < 0 && (Manual_focus % 2) == 1) Manual_focus--;
	if (dx > 0 && (Manual_focus % 2) == 0 && Manual_focus + 1 < NUM_SECTIONS) Manual_focus++;
	if (dy < 0 && Manual_focus >= 2) Manual_focus -= 2;
	if (dy > 0 && Manual_focus + 2 < NUM_SECTIONS) Manual_focus += 2;
}

void n3ds_manual_select(void) { open_section(Manual_focus); }

void n3ds_manual_touch(int held, int tx, int ty, int pressed)
{
	if (pressed) {
		Manual_pressed = -1;
		for (int i = 0; i < NUM_SECTIONS; i++) {
			int x, y, w, h;
			button_rect(i, &x, &y, &w, &h);
			if (tx >= x && tx < x + w && ty >= y && ty < y + h) {
				Manual_pressed = i;
				n3ds_click();
				break;
			}
		}
	} else if (!held && Manual_pressed >= 0) {
		open_section(Manual_pressed);
		Manual_pressed = -1;
	}
}

/* top screen: the section text, 400x240, scrolled */
void n3ds_manual_draw_text(void)
{
	const manual_section &p = Sections[Manual_section];
	gr_set_clip(0, 0, 400, 240, false);
	gr_set_color(6, 10, 18);
	gr_rect(0, 0, 400, 240, false);

	int n = 0;
	while (n < 40 && p.lines[n]) n++;
	Manual_max_scroll = n * LINE_H + 8 - (TEXT_BOTTOM - TEXT_TOP);
	if (Manual_max_scroll < 0) Manual_max_scroll = 0;
	if (Manual_scroll > Manual_max_scroll) Manual_scroll = (float)Manual_max_scroll;

	gr_set_font(FONT1);
	gr_set_color(230, 230, 230);
	int y = TEXT_TOP + 4 - (int)Manual_scroll;
	for (int i = 0; i < n; i++, y += LINE_H) {
		if (y < TEXT_TOP - LINE_H || y > TEXT_BOTTOM) continue;
		gr_string(8, y, p.lines[i], false);
	}

	/* header and footer drawn over the text so scrolled lines slide under them */
	gr_set_color(24, 44, 74);
	gr_rect(0, 0, 400, TEXT_TOP - 2, false);
	gr_set_font(FONT2);
	gr_set_color(255, 220, 120);
	gr_string(8, 3, p.title, false);
	gr_set_font(FONT1);
	char buf[32];
	sprintf(buf, "%d / %d", Manual_section + 1, NUM_SECTIONS);
	int w, h;
	gr_get_string_size(&w, &h, buf);
	gr_set_color(180, 180, 180);
	gr_string(390 - w, 7, buf, false);

	gr_set_color(24, 44, 74);
	gr_rect(0, TEXT_BOTTOM + 2, 400, 240 - TEXT_BOTTOM - 2, false);
	if (Manual_max_scroll > 0) {
		int track = TEXT_BOTTOM - TEXT_TOP;
		int bar = track * track / (track + Manual_max_scroll);
		int pos = (int)((track - bar) * Manual_scroll / Manual_max_scroll);
		gr_set_color(40, 70, 110);
		gr_rect(395, TEXT_TOP, 3, track, false);
		gr_set_color(140, 180, 230);
		gr_rect(395, TEXT_TOP + pos, 3, bar, false);
		gr_set_color(170, 170, 170);
		gr_string(8, TEXT_BOTTOM + 3, "Circle Pad / C-Stick: scroll", false);
	}
	gr_reset_clip();
}

/* touchscreen: the section buttons, 320x240 */
void n3ds_manual_draw(void)
{
	gr_set_clip(0, 0, 320, 240, false);
	gr_set_color(6, 10, 18);
	gr_rect(0, 0, 320, 240, false);

	gr_set_color(24, 44, 74);
	gr_rect(0, 0, 320, 22, false);
	gr_set_font(FONT2);
	gr_set_color(255, 220, 120);
	gr_string(6, 3, "MANUAL", false);
	gr_set_font(FONT1);

	for (int i = 0; i < NUM_SECTIONS; i++) {
		int x, y, w, h;
		button_rect(i, &x, &y, &w, &h);
		int pressed = (i == Manual_pressed);
		int cur = (i == Manual_section);
		int off = pressed ? 1 : 0;   /* pressed buttons sink a pixel and light up */
		if (pressed)    gr_set_color(90, 140, 200);
		else if (cur)   gr_set_color(50, 90, 140);
		else            gr_set_color(30, 52, 84);
		gr_rect(x, y + off, w, h, false);
		if (i == Manual_focus) {
			gr_set_color(255, 220, 120);
			gr_rect(x, y + off, w, 2, false);
			gr_rect(x, y + off + h - 2, w, 2, false);
			gr_rect(x, y + off, 2, h, false);
			gr_rect(x + w - 2, y + off, 2, h, false);
		}
		int tw, th;
		gr_get_string_size(&tw, &th, Sections[i].title);
		gr_set_color(240, 240, 240);
		gr_string(x + (w - tw) / 2, y + off + (h - th) / 2, Sections[i].title, false);
	}

	gr_set_color(24, 44, 74);
	gr_rect(0, 210, 320, 30, false);
	gr_set_color(200, 200, 200);
	gr_string(6, 212, "D-Pad: choose   A: open   L / R: section", false);
	gr_string(6, 225, "Tap a section      B / START: close", false);
	gr_reset_clip();
}
