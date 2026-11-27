/*
 * Built-in user manual for FreeSpace 2 on the New 3DS.
 *
 * Same controls as every other 3DS port:
 *   Circle Pad / C-Stick   scroll the text (hold to keep scrolling); or drag it with the stylus
 *   D-Pad                  move between the section buttons (touchscreen); a section opens as it is picked
 *   A                      press LEFT HAND or CLOSE when the D-Pad is on it
 *   L / R                  previous / next section
 *   tap                    open a section
 *   B or START, or CLOSE   close
 * The text is on the top screen, the section buttons on the touchscreen.
 * Open with SELECT + START (pauses the game in flight) or the MANUAL button.
 */
#include "n3ds/n3ds_panel.h"
#include "n3ds/n3ds_manual.h"

#include <string.h>

#include "globalincs/pstypes.h"
#include "graphics/2d.h"
#include "graphics/font.h"

extern "C" void n3ds_click(void);

struct manual_section {
	const char *title;
	const char *lines[56];
};

static const manual_section Sections[] = {
	{ "WELCOME", {
		"FreeSpace 2 for the New 3DS (fs2_open 3.7.2).",
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
		"screen, and buttons: BACK, ENTER, ZOOM and",
		"MANUAL, and above them the screen's own",
		"(COMMIT, SHIPS, WEAPONS, READY ROOM...).",
		"Away from the buttons it is a touchpad:",
		"slide to move the pointer, tap to click.",
		"",
		"TOP SCREEN",
		"With the 3D slider all the way down, the top",
		"screen uses its sharper 800-pixel-wide mode.",
		NULL } },
	{ "FLYING", {
		"Circle Pad    pitch and turn",
		"C-Stick       roll",
		"ZR            throttle up (hold)",
		"SELECT+ZR     throttle down (hold)",
		"ZL            swap screens",
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
		"START         pause menu: RESUME, MANUAL,",
		"              SETTINGS, END MISSION, EXIT",
		"              GAME. The game is paused while",
		"              it is open.",
		"SELECT (tap)  comms menu (wingman orders)",
		"",
		"END MISSION asks first: No / Yes, Quit /",
		"Yes, Restart, as three big buttons on the",
		"touchscreen. The focus starts on No.",
		"",
		"LEFT-HANDED   LEFT HAND (in this manual or",
		"              in START > SETTINGS) swaps",
		"              the D-Pad with Y/A/B/X (saved).",
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
		"B             panel highlight on / off",
		"START         open this manual",
		NULL } },
	{ "COCKPIT PANEL", {
		"Top strip: current target - name, class, hull",
		"strength and distance.",
		"",
		"HULL, SHLD, BURN (afterburner fuel) and WPN",
		"(weapon energy) bars show your ship.",
		"SPEED is your speed, THR your throttle setting.",
		"An M beside SPEED: matching the target's speed.",
		"PRI / SEC: selected weapons and missiles left.",
		"The box below shows training text, directives",
		"and the latest radio messages.",
		"",
		"A red line starting with ! is a warning: an",
		"enemy has its guns on you, is locking a",
		"missile on, or has locked on.",
		"",
		"A training message too long for the box is",
		"shown a page at a time. Its last line says",
		"PAGE 1/2: tap the box for the next page. The",
		"page stays until you turn it.",
		"",
		"ENERGY (S W E): tap the top half of a column to",
		"give that system more power, the bottom half to",
		"give it less.",
		"",
		"Buttons, top row (targets): ENEMY nearest",
		"hostile, NEXT next ship, FRIEND next friendly,",
		"SUB next subsystem, ATTKR closest attacker.",
		"Bottom row: MATCH matches the target's speed,",
		"COMMS opens wingman orders, EQ SH evens your",
		"shields, BOMB targets bombs, RADAR changes the",
		"radar range.",
		"",
		"WITHOUT TOUCH (screens swapped with ZL):",
		"SELECT+B puts a gold highlight on the panel.",
		"The D-Pad moves it over the buttons, the",
		"energy columns and the message box; A presses",
		"what it is on (on the box: next page).",
		"B or SELECT+B turns it off. While it is on,",
		"the D-Pad and A work the panel only.",
		NULL } },
	{ "MENUS", {
		"Circle Pad / C-Stick  move the pointer",
		"Slide on dashboard    move the pointer",
		"A or tap dashboard    click",
		"R + tap dashboard     right-click",
		"X                     Enter (accept)",
		"B                     back (Esc)",
		"D-Pad                 arrow keys",
		"L / R                 zoom the view out / in",
		"ZOOM button           cycle the zoom levels",
		"ZL                    swap screens",
		"",
		"Away from its buttons the dashboard is a",
		"touchpad, like a laptop's: slide to move the",
		"pointer, tap to click where it points.",
		"",
		"SCREEN BUTTONS",
		"Above BACK, ENTER, ZOOM and MANUAL the",
		"dashboard shows the buttons of the screen you",
		"are on. Main hall: READY ROOM, TECH ROOM,",
		"OPTIONS. Briefing and loadout: BRIEFING,",
		"SHIPS, WEAPONS, COMMIT. Command briefing:",
		"CONTINUE. Debriefing: NEXT TAB, ACCEPT.",
		"",
		"MESSAGES",
		"When the game asks a question in a pop-up,",
		"the dashboard shows the text and one big",
		"button per answer: tap one, or D-Pad and A.",
		"B cancels. The focus starts on No or Cancel.",
		"",
		"SCREENS SWAPPED (ZL)",
		"The whole menu is on the touchscreen: tap it",
		"with the stylus like a mouse. L / R and ZOOM",
		"zoom it in to read small text; the Circle Pad",
		"or C-Stick then moves the view. The dashboard",
		"fills the top screen: the D-Pad moves a gold",
		"highlight over its buttons, A presses one.",
		"In this manual, drag the text to scroll.",
		"Press ZL again to swap back.",
		"",
		"The top screen starts with the whole screen.",
		"Zoom in (R) to read small text; the view then",
		"scrolls when the pointer nears its edge.",
		"",
		"Main hall: READY ROOM starts the campaign. Or",
		"point at a room - its name shows on the",
		"dashboard - then press A to go in.",
		"",
		"Pilot files, settings and saves are kept in",
		"sdmc:/3ds/freespace2/.fs2_open",
		NULL } },
	{ "FILES & SAVES", {
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
		"",
		"Program:  sdmc:/3ds/fs2-3ds.3dsx (directly in",
		"          /3ds, not in the game folder)",
		"Pilots:   sdmc:/3ds/freespace2/.fs2_open",
		"",
		"A pilot or campaign file is written to a",
		"temporary file first and then renamed, so a",
		"failed save cannot damage the old one.",
		"",
		"START > EXIT GAME asks YES / NO and says",
		"what happens: your pilot is saved first (once",
		"you have one), then the game closes. HOME >",
		"Close does the same. The pilot file holds",
		"your campaign progress up to the last",
		"mission you finished. A mission in flight",
		"cannot be saved: leave in the middle of one",
		"and you fly it again next time. Pressing",
		"HOME only pauses the game's clock; it saves",
		"nothing by itself.",
		"",
		"LOGS, in sdmc:/3ds/freespace2/",
		"fs2_3ds.log   start, exit, memory headroom",
		"              after each mission loads, and",
		"              errors. Rolls over at 1 MB.",
		"",
		"The full debug log (slower) is a setting:",
		"START > SETTINGS > DEBUG LOG, off unless you",
		"switch it on; it stays as you set it. A",
		"debug.txt file is no longer used.",
		"",
		"If the game crashes, a crash screen shows",
		"on both screens: press START to return to",
		"the Homebrew Menu. Details are in the log.",
		"",
		"An original 3DS or 2DS is told the game",
		"needs a New 3DS.",
		NULL } },
};
#define NUM_SECTIONS ((int)(sizeof(Sections) / sizeof(Sections[0])))

static int Manual_open = 0;
static int Manual_section = 0;     /* section shown */
static int Manual_focus = 0;       /* section button highlighted by the D-Pad */
static float Manual_scroll = 0;    /* pixels */
static int Manual_max_scroll = 0;
static int Manual_pressed = -1;    /* touched button, for the press animation */
static int Manual_flash = 0;       /* frames left of an A press's animation (highlighted button) */
static int Manual_close_asked = 0; /* CLOSE was pressed: the platform closes the manual as B does */

/* 1 once after CLOSE was pressed (tap or A) */
int n3ds_manual_take_close(void) { int c = Manual_close_asked; Manual_close_asked = 0; return c; }

#define LINE_H 13
#define TEXT_TOP 24
#define TEXT_BOTTOM 224

int n3ds_manual_is_open(void) { return Manual_open; }
void n3ds_manual_toggle(void) { Manual_open = !Manual_open; Manual_focus = Manual_section; Manual_pressed = -1; Manual_close_asked = 0; }
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
/* FS2-U6: under them a row with LEFT HAND and CLOSE (house manual: a Left hand toggle and a
   Close button). The control hints moved to the strip under the text, so the buttons use the
   whole touchscreen: 152 x 44, 4 / 6 px apart, 6 px from the edges, the last row ends at y 222. */
#define BTN_LEFTY NUM_SECTIONS
#define BTN_CLOSE (NUM_SECTIONS + 1)
#define NUM_BUTTONS (NUM_SECTIONS + 2)
static void button_rect(int i, int *x, int *y, int *w, int *h)
{
	*w = 152; *h = 44;
	*x = 6 + (i % 2) * 156;
	*y = 28 + (i / 2) * 50;
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

void n3ds_manual_scroll_by(float px)
{
	Manual_scroll += px;
	if (Manual_scroll < 0) Manual_scroll = 0;
	if (Manual_scroll > Manual_max_scroll) Manual_scroll = (float)Manual_max_scroll;
}

void n3ds_manual_dpad(int dx, int dy)
{
	/* two columns: the section buttons, then the LEFT HAND / CLOSE row (NUM_SECTIONS is even) */
	if (dx < 0 && (Manual_focus % 2) == 1) Manual_focus--;
	if (dx > 0 && (Manual_focus % 2) == 0 && Manual_focus + 1 < NUM_BUTTONS) Manual_focus++;
	if (dy < 0 && Manual_focus >= 2) Manual_focus -= 2;
	if (dy > 0 && Manual_focus + 2 < NUM_BUTTONS) Manual_focus += 2;
	/* house standard: a section opens as the D-Pad picks it (A is only needed on LEFT HAND and CLOSE) */
	if (Manual_focus < NUM_SECTIONS && Manual_focus != Manual_section) open_section(Manual_focus);
}

void n3ds_manual_select(void)
{
	/* the same press animation and click as a tap */
	n3ds_click();
	Manual_flash = 6;
	if (Manual_focus == BTN_LEFTY) { n3ds_set_lefty(!n3ds_lefty()); return; }
	if (Manual_focus == BTN_CLOSE) { Manual_close_asked = 1; return; }
	open_section(Manual_focus);
}

void n3ds_manual_touch(int held, int tx, int ty, int pressed)
{
	/* drag to scroll (rule 13): a touch that moves more than 6 px scrolls the text on the
	   top screen and presses no button; a little momentum after the stylus is lifted */
	static int x0, y0, ylast, dragging;
	static float mom;
	if (pressed) { x0 = tx; y0 = ty; ylast = ty; dragging = 0; mom = 0; }
	else if (held) {
		if (!dragging && (tx - x0 > 6 || x0 - tx > 6 || ty - y0 > 6 || y0 - ty > 6)) { dragging = 1; Manual_pressed = -1; }
		if (dragging) { mom = (float)(ylast - ty); n3ds_manual_scroll_by(mom); }
		ylast = ty;
		if (dragging) return;
	} else if (dragging) { dragging = 0; return; }
	else if (mom > 0.5f || mom < -0.5f) { n3ds_manual_scroll_by(mom); mom *= 0.88f; }
	if (pressed) {
		Manual_pressed = -1;
		for (int i = 0; i < NUM_BUTTONS; i++) {
			int x, y, w, h;
			button_rect(i, &x, &y, &w, &h);
			if (tx >= x && tx < x + w && ty >= y && ty < y + h) {
				Manual_pressed = i;
				n3ds_click();
				break;
			}
		}
	} else if (!held && Manual_pressed >= 0) {
		if (Manual_pressed == BTN_LEFTY) n3ds_set_lefty(!n3ds_lefty());
		else if (Manual_pressed == BTN_CLOSE) Manual_close_asked = 1;
		else open_section(Manual_pressed);
		Manual_pressed = -1;
	}
}

/* top screen: the section text, 400x240, scrolled */
void n3ds_manual_draw_text(void)
{
	const manual_section &p = Sections[Manual_section];
	gr_set_clip(0, 0, 400, 240, false);
	gr_set_color(8, 12, 40);      /* house navy */
	n3ds_rect(0, 0, 400, 240);

	int n = 0;
	while (n < 56 && p.lines[n]) n++;
	Manual_max_scroll = n * LINE_H + 8 - (TEXT_BOTTOM - TEXT_TOP);
	if (Manual_max_scroll < 0) Manual_max_scroll = 0;
	if (Manual_scroll > Manual_max_scroll) Manual_scroll = (float)Manual_max_scroll;

	gr_set_font(FONT1);
	gr_set_color(230, 230, 230);
	int y = TEXT_TOP + 4 - (int)Manual_scroll;
	for (int i = 0; i < n; i++, y += LINE_H) {
		/* FS2-U6: whole lines only. A line that would be cut by the header or the hint strip is
		   left out until it has scrolled fully into view (the last line used to show cut in half). */
		if (y < TEXT_TOP || y + 10 > TEXT_BOTTOM) continue;
		gr_string(8, y, p.lines[i], false);
	}

	/* header and footer drawn over the text so scrolled lines slide under them */
	gr_set_color(40, 56, 120);    /* house bar */
	n3ds_rect(0, 0, 400, TEXT_TOP - 2);
	gr_set_font(FONT2);
	gr_set_color(255, 204, 64);
	gr_string(8, 3, p.title, false);
	gr_set_font(FONT1);
	char buf[32];
	sprintf(buf, "%d / %d", Manual_section + 1, NUM_SECTIONS);
	int w, h;
	gr_get_string_size(&w, &h, buf);
	gr_set_color(180, 180, 180);
	gr_string(390 - w, 7, buf, false);

	/* FS2-U6: the house hint strip (16 px at y 224, dark, gold, centred) under the text, in the
	   house wording; it follows left-hand mode. The first wording that fits is used. */
	gr_set_color(4, 6, 20);
	n3ds_rect(0, TEXT_BOTTOM, 400, 240 - TEXT_BOTTOM);
	{
		static const char *const hr[] = { "Sticks: scroll  D-Pad, L / R: section  B / START: close",
			"Sticks: scroll  D-Pad, L/R: section  B/START: close", "Sticks: scroll  L / R: section  START: close" };
		static const char *const hl[] = { "Sticks: scroll  A/B/X/Y, L/R: section  D-Down/START: close",
			"Sticks: scroll  ABXY, L/R: section  START: close", "Sticks: scroll  L / R: section  START: close" };
		const char *const *hs = n3ds_lefty() ? hl : hr;
		const char *hint = hs[2];
		int hw = 0, hh = 9;
		for (int i = 0; i < 3; i++) {
			gr_get_string_size(&hw, &hh, hs[i]);
			if (hw <= 400 - 8) { hint = hs[i]; break; }
		}
		gr_get_string_size(&hw, &hh, hint);
		gr_set_color(255, 204, 64);
		gr_string((400 - hw) / 2, TEXT_BOTTOM + (16 - hh) / 2, hint, false);
	}
	if (Manual_max_scroll > 0) {
		int track = TEXT_BOTTOM - TEXT_TOP;
		int bar = track * track / (track + Manual_max_scroll);
		int pos = (int)((track - bar) * Manual_scroll / Manual_max_scroll);
		gr_set_color(20, 30, 70);
		n3ds_rect(395, TEXT_TOP, 3, track);
		gr_set_color(120, 150, 230);
		n3ds_rect(395, TEXT_TOP + pos, 3, bar);
	}
	gr_reset_clip();
}

/* touchscreen: the section buttons, 320x240 */
void n3ds_manual_draw(void)
{
	gr_set_clip(0, 0, 320, 240, false);
	gr_set_color(8, 12, 40);      /* house navy */
	n3ds_rect(0, 0, 320, 240);

	gr_set_color(40, 56, 120);    /* house bar */
	n3ds_rect(0, 0, 320, 22);
	gr_set_font(FONT2);
	gr_set_color(255, 204, 64);
	gr_string(6, 3, "MANUAL", false);
	gr_set_font(FONT1);

	for (int i = 0; i < NUM_BUTTONS; i++) {
		int x, y, w, h;
		button_rect(i, &x, &y, &w, &h);
		int pressed = (i == Manual_pressed) || (Manual_flash > 0 && i == Manual_focus);
		int cur = (i == BTN_LEFTY) ? n3ds_lefty() : (i == BTN_CLOSE) ? 0 : (i == Manual_section);
		int off = pressed ? 1 : 0;   /* pressed buttons sink a pixel and light up */
		if (pressed)    gr_set_color(220, 180, 60);   /* house pressed */
		else if (cur)   gr_set_color(64, 88, 170);
		else            gr_set_color(40, 56, 120);    /* house face */
		n3ds_rect(x, y + off, w, h);
		gr_set_color(120, 150, 230);              /* house button edge */
		n3ds_rect(x, y + off, w, 1);
		n3ds_rect(x, y + off + h - 1, w, 1);
		n3ds_rect(x, y + off, 1, h);
		n3ds_rect(x + w - 1, y + off, 1, h);
		if (i == Manual_focus) {
			gr_set_color(255, 204, 64);
			n3ds_rect(x, y + off, w, 2);
			n3ds_rect(x, y + off + h - 2, w, 2);
			n3ds_rect(x, y + off, 2, h);
			n3ds_rect(x + w - 2, y + off, 2, h);
		}
		int tw, th;
		const char *label = (i == BTN_LEFTY) ? (n3ds_lefty() ? "LEFT HAND: ON" : "LEFT HAND: OFF") : (i == BTN_CLOSE) ? "CLOSE" : Sections[i].title;
		gr_get_string_size(&tw, &th, label);
		if (pressed) gr_set_color(0, 0, 0);       /* house: dark text on the gold press */
		else         gr_set_color(240, 240, 240);
		gr_string(x + (w - tw) / 2, y + off + (h - th) / 2, label, false);
	}

	if (Manual_flash > 0) Manual_flash--;

	gr_reset_clip();
}
