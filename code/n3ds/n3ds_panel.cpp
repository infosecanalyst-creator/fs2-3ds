/*
 * Bottom-screen cockpit panel for FreeSpace 2 on the New 3DS.
 *
 * Layout (320x240, touch):
 *   +-------------------------------------------------+
 *   | TARGET  name / class          hull %   dist     |
 *   |---------------------------------------------------|
 *   | HULL [####]  SHLD [####]  SPEED nn / THR [###]   |
 *   |---------------------------------------------------|
 *   |  ENERGY   S  W  E   (tap top half +, bottom -)   |
 *   |---------------------------------------------------|
 *   | HOSTILE | NEXT    | FRIEND | SUBSYS | ATTACKER   |
 *   | MATCH   | ORDERS  | EQ SHLD| BOMB   | RADAR      |
 *   +-------------------------------------------------+
 */
#include "n3ds/n3ds_panel.h"

#include "globalincs/pstypes.h"
#include "graphics/2d.h"
#include "graphics/font.h"
#include "io/key.h"
#include "object/object.h"
#include "ship/ship.h"
#include "ai/ai.h"
#include "hud/hudets.h"
#include "math/vecmat.h"
#include "playerman/player.h"
#include "weapon/weapon.h"
#include "hud/hudconfig.h"
#include "hud/hudgauges.h"
#include "io/timer.h"
#include <string.h>

extern "C" void n3ds_click(void);

#define PANEL_W 320
#define PANEL_H 240

struct panel_button {
	int x, y, w, h;
	const char *label;
	int key;
};

static const panel_button Panel_buttons[] = {
	{   2, 176, 62, 30, "HOSTILE",  KEY_H },
	{  66, 176, 62, 30, "NEXT",     KEY_T },
	{ 130, 176, 62, 30, "FRIEND",   KEY_F },
	{ 194, 176, 62, 30, "SUBSYS",   KEY_S },
	{ 258, 176, 60, 30, "ATTACK",  KEY_R },
	{   2, 208, 62, 30, "MATCH",    KEY_M },
	{  66, 208, 62, 30, "ORDERS",   KEY_C },
	{ 130, 208, 62, 30, "SHIELDS",  KEY_Q },
	{ 194, 208, 62, 30, "BOMB",     KEY_B },
	{ 258, 208, 60, 30, "RADAR",    KEY_PADPLUS },
};
#define NUM_PANEL_BUTTONS ((int)(sizeof(Panel_buttons) / sizeof(Panel_buttons[0])))

/* energy transfer columns: shields, weapons, engines (narrow, right edge) */
struct ets_column { int x; const char *label; int key_up; int key_down; };
static const ets_column Ets_cols[3] = {
	{ 252, "S", KEY_HOME,   KEY_END },
	{ 274, "W", KEY_INSERT, KEY_DELETE },
	{ 296, "E", KEY_PAGEUP, KEY_PAGEDOWN },
};
#define ETS_W 20
#define ETS_Y 44
#define ETS_H 126
#define TEXT_W 244          /* left area for text, x 4 .. 248 */

static int Panel_pressed_key = -1;
static int Panel_pressed_button = -1;
static int Panel_pressed_ets = -1;      /* energy cell being pressed: column * 2 + (1 = lower half) */

/* ---- buttons without touch: a gold highlight moved by the D-Pad over the 10 buttons and the
   6 energy cells (top / bottom half of each column), A presses it */
#define NAV_TARGETS (NUM_PANEL_BUTTONS + 6)
static int Panel_nav = 0;
static int Panel_focus = 0;
static int Panel_nav_hold = 0;          /* frames left of an A press (key held, button sunk) */

static void nav_rect(int i, int *x, int *y, int *w, int *h)
{
	if (i < NUM_PANEL_BUTTONS) {
		const panel_button &b = Panel_buttons[i];
		*x = b.x; *y = b.y; *w = b.w; *h = b.h;
	} else {
		int c = (i - NUM_PANEL_BUTTONS) / 2, lower = (i - NUM_PANEL_BUTTONS) & 1;
		*x = Ets_cols[c].x; *w = ETS_W; *h = ETS_H / 2;
		*y = ETS_Y + (lower ? ETS_H / 2 : 0);
	}
}

/* ---- recent HUD messages (the top-screen message lines are off on the 3DS) */
#define PANEL_MSGS 4
static char Panel_msg[PANEL_MSGS][160];
static int Panel_msg_time[PANEL_MSGS];
static int Panel_msg_head = 0;

void n3ds_panel_message(int source, const char *msg)
{
	(void)source;
	strncpy(Panel_msg[Panel_msg_head], msg, 159);
	Panel_msg[Panel_msg_head][159] = 0;
	Panel_msg_time[Panel_msg_head] = timestamp();
	Panel_msg_head = (Panel_msg_head + 1) % PANEL_MSGS;
}

extern int n3ds_training_text(char *out, int outlen);
extern int n3ds_directives(char lines[][128], int *status, int max);

/* top-screen HUD gauges that are mostly small text: hidden, their information is on
   this panel instead (the HUD keeps reticle, radar, shields, brackets, arrows) */
static const int Hidden_gauges[] = {
	HUD_MISSION_TIME, HUD_TARGET_MONITOR, HUD_TARGET_MONITOR_EXTRA_DATA, HUD_ETS_GAUGE,
	HUD_AUTO_TARGET, HUD_AUTO_SPEED, HUD_WEAPONS_GAUGE, HUD_ESCORT_VIEW, HUD_DIRECTIVES_VIEW,
	HUD_TALKING_HEAD, HUD_DAMAGE_GAUGE, HUD_MESSAGE_LINES, HUD_CMEASURE_GAUGE, HUD_WINGMEN_STATUS,
	HUD_OFFSCREEN_RANGE, HUD_KILLS_GAUGE, HUD_MESSAGE_BOX, HUD_SUPPORT_GAUGE,
};

static void hide_text_gauges(void)
{
	for (unsigned i = 0; i < sizeof(Hidden_gauges) / sizeof(Hidden_gauges[0]); i++)
		hud_config_set_gauge_flags(Hidden_gauges[i], 0, 0);
}

static void panel_bar(int x, int y, int w, int h, float frac, int r, int g, int b)
{
	if (frac < 0.0f) frac = 0.0f;
	if (frac > 1.0f) frac = 1.0f;
	gr_set_color(40, 40, 40);
	gr_rect(x, y, w, h, false);
	gr_set_color(r, g, b);
	gr_rect(x, y, (int)(w * frac), h, false);
}

static void panel_vbar(int x, int y, int w, int h, float frac, int r, int g, int b)
{
	if (frac < 0.0f) frac = 0.0f;
	if (frac > 1.0f) frac = 1.0f;
	gr_set_color(40, 40, 40);
	gr_rect(x, y, w, h, false);
	int fh = (int)(h * frac);
	gr_set_color(r, g, b);
	gr_rect(x, y + h - fh, w, fh, false);
}

static int text_w(const char *s)
{
	int w, h;
	gr_get_string_size(&w, &h, (char *)s);
	return w;
}

/* word-wrap text into lines of at most maxw pixels; returns the line count */
static int wrap_lines(const char *text, int maxw, char out[][96], int maxlines)
{
	int n = 0;
	const char *p = text;
	while (*p && n < maxlines) {
		while (*p == ' ' || *p == '\n') p++;
		if (!*p) break;
		char line[96];
		int len = 0, last_space = -1;
		while (p[len] && p[len] != '\n' && len < 95) {
			memcpy(line, p, len + 1); line[len + 1] = 0;
			if (text_w(line) > maxw) break;
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

#define BOX_Y 96
#define BOX_BOTTOM 172
#define LINE_H 10

void n3ds_panel_draw(void)
{
	hide_text_gauges();

	gr_set_clip(0, 0, PANEL_W, PANEL_H, false);
	gr_set_color(8, 14, 20);
	gr_rect(0, 0, PANEL_W, PANEL_H, false);
	gr_set_font(FONT1);

	char buf[160];

	/* ---- target strip */
	gr_set_color(30, 50, 70);
	gr_rect(0, 0, PANEL_W, 30, false);
	if (Player_ai && Player_ai->target_objnum >= 0 && Player_obj) {
		object *tobj = &Objects[Player_ai->target_objnum];
		char right1[32] = "", right2[32];
		float dist = vm_vec_dist(&Player_obj->pos, &tobj->pos);
		sprintf(right2, "%dm", (int)dist);
		const char *name = "TARGET", *cls = "";
		if (tobj->type == OBJ_SHIP) {
			ship *tsp = &Ships[tobj->instance];
			name = tsp->ship_name;
			cls = Ship_info[tsp->ship_info_index].name;
			sprintf(right1, "HULL %d%%", (int)(get_hull_pct(tobj) * 100.0f + 0.5f));
		}
		gr_set_color(255, 200, 80);
		gr_string(4, 4, (char *)name, false);
		gr_set_color(200, 200, 200);
		gr_string(4, 17, (char *)cls, false);
		gr_set_color(255, 255, 255);
		gr_string(PANEL_W - 4 - text_w(right1), 4, right1, false);
		gr_string(PANEL_W - 4 - text_w(right2), 17, right2, false);
	} else {
		gr_set_color(140, 140, 140);
		gr_string(4, 10, "NO TARGET", false);
	}

	if (Player_obj && Player_ship) {
		/* ---- own ship: two bars per row */
		ship_info *sip = &Ship_info[Player_ship->ship_info_index];
		float smax = sip->max_shield_strength, s = 0.0f;
		for (int i = 0; i < MAX_SHIELD_SECTIONS; i++) s += Player_obj->shield_quadrant[i];
		gr_set_color(200, 200, 200);
		gr_string(4, 34, "HULL", false);
		panel_bar(44, 35, 72, 7, get_hull_pct(Player_obj), 80, 220, 80);
		gr_set_color(200, 200, 200);
		gr_string(122, 34, "SHLD", false);
		panel_bar(162, 35, 82, 7, smax > 0.0f ? s / smax : 0.0f, 80, 160, 255);

		gr_set_color(200, 200, 200);
		gr_string(4, 46, "AFTB", false);
		panel_bar(44, 47, 72, 7, sip->afterburner_fuel_capacity > 0.0f ? Player_ship->afterburner_fuel / sip->afterburner_fuel_capacity : 0.0f, 255, 120, 60);
		gr_set_color(200, 200, 200);
		gr_string(122, 46, "WPN", false);
		panel_bar(162, 47, 82, 7, sip->max_weapon_reserve > 0.0f ? Player_ship->weapon_energy / sip->max_weapon_reserve : 0.0f, 220, 220, 80);

		sprintf(buf, "SPD %d", (int)Player_obj->phys_info.fspeed);
		gr_set_color(200, 200, 200);
		gr_string(4, 58, buf, false);
		if (Player && (Player->flags & PLAYER_FLAGS_MATCH_TARGET)) {
			/* matching the target's speed */
			gr_set_color(255, 220, 80);
			gr_string(4 + text_w(buf) + 8, 58, "M", false);
		}
		gr_string(122, 58, "THR", false);
		panel_bar(162, 59, 82, 7, Player_obj->phys_info.forward_thrust, 255, 180, 60);

		/* ---- weapons */
		ship_weapon *swp = &Player_ship->weapons;
		int pb = swp->current_primary_bank, sb = swp->current_secondary_bank;
		gr_set_color(120, 200, 255);
		gr_string(4, 72, "PRI", false);
		gr_set_color(235, 235, 235);
		if (pb >= 0 && pb < swp->num_primary_banks && swp->primary_bank_weapons[pb] >= 0) {
			strcpy(buf, Weapon_info[swp->primary_bank_weapons[pb]].name);
			if (Player_ship->flags & SF_PRIMARY_LINKED) {
				char tmp[160];
				sprintf(tmp, "%s (linked)", buf);
				if (text_w(tmp) <= TEXT_W - 36) strcpy(buf, tmp);
			}
		} else strcpy(buf, "-");
		gr_string(36, 72, buf, false);

		gr_set_color(120, 200, 255);
		gr_string(4, 84, "SEC", false);
		gr_set_color(235, 235, 235);
		if (sb >= 0 && sb < swp->num_secondary_banks && swp->secondary_bank_weapons[sb] >= 0) {
			sprintf(buf, "%s  x%d", Weapon_info[swp->secondary_bank_weapons[sb]].name, swp->secondary_bank_ammo[sb]);
			if (text_w(buf) > TEXT_W - 36) strcpy(buf, Weapon_info[swp->secondary_bank_weapons[sb]].name);
		} else strcpy(buf, "-");
		gr_string(36, 84, buf, false);

		/* ---- text box: training message, directives, recent messages. Whole lines only:
		   anything that would not fit completely is left out. */
		gr_set_color(18, 28, 40);
		gr_rect(2, BOX_Y - 2, TEXT_W + 4, BOX_BOTTOM - BOX_Y + 2, false);
		int y = BOX_Y;
		char lines[8][96];
		char tbuf[512];
		if (n3ds_training_text(tbuf, sizeof(tbuf))) {
			char tl[16][96];
			int n = wrap_lines(tbuf, TEXT_W - 4, tl, 16);
			int fit = (BOX_BOTTOM - y) / LINE_H - 2;          /* keep room for the key prompt */
			if (fit < 2) fit = 2;
			int first = 0;
			if (n > fit) {
				/* too long for the box: show it a page at a time, turning every 3 s */
				int pages = (n + fit - 1) / fit;
				first = ((timestamp() / 3000) % pages) * fit;
			}
			gr_set_color(140, 200, 255);
			for (int i = first; i < n && i < first + fit; i++, y += LINE_H)
				gr_string(5, y, tl[i], false);
		}
		char dl[3][128];
		int dst[3];
		int nd = n3ds_directives(dl, dst, 3);
		for (int d = 0; d < nd; d++) {
			int n = wrap_lines(dl[d], TEXT_W - 4, lines, 3);
			if (y + n * LINE_H > BOX_BOTTOM) break;
			switch (dst[d]) {
				case 1: gr_set_color(100, 255, 100); break;
				case 2: gr_set_color(255, 90, 90); break;
				case 3: gr_set_color(110, 150, 255); break;
				default: gr_set_color(255, 255, 255); break;
			}
			for (int i = 0; i < n; i++, y += LINE_H)
				gr_string(5, y, lines[i], false);
		}
		/* newest messages first, shown for 12 seconds */
		for (int k = 1; k <= PANEL_MSGS; k++) {
			int m = (Panel_msg_head - k + PANEL_MSGS) % PANEL_MSGS;
			if (!Panel_msg[m][0] || timestamp() - Panel_msg_time[m] > 12000) continue;
			int n = wrap_lines(Panel_msg[m], TEXT_W - 4, lines, 3);
			if (y + n * LINE_H > BOX_BOTTOM) break;
			gr_set_color(200, 230, 200);
			for (int i = 0; i < n; i++, y += LINE_H)
				gr_string(5, y, lines[i], false);
		}

		/* ---- ETS */
		int idx[3] = { Player_ship->shield_recharge_index, Player_ship->weapon_recharge_index, Player_ship->engine_recharge_index };
		for (int c = 0; c < 3; c++) {
			const ets_column &e = Ets_cols[c];
			gr_set_color(200, 200, 200);
			gr_string(e.x + (ETS_W - text_w(e.label)) / 2, 34, (char *)e.label, false);
			panel_vbar(e.x, ETS_Y, ETS_W, ETS_H, Energy_levels[idx[c]], 120, 200, 255);
			if (Panel_pressed_ets >= 0 && Panel_pressed_ets / 2 == c) {
				/* pressed half: a light frame around it */
				int py = ETS_Y + ((Panel_pressed_ets & 1) ? ETS_H / 2 : 0);
				gr_set_color(200, 230, 255);
				gr_rect(e.x, py, ETS_W, 2, false);
				gr_rect(e.x, py + ETS_H / 2 - 2, ETS_W, 2, false);
				gr_rect(e.x, py, 2, ETS_H / 2, false);
				gr_rect(e.x + ETS_W - 2, py, 2, ETS_H / 2, false);
			}
			gr_set_color(255, 255, 255);
			gr_string(e.x + (ETS_W - text_w("+")) / 2, ETS_Y + 2, "+", false);
			gr_string(e.x + (ETS_W - text_w("-")) / 2, ETS_Y + ETS_H - 12, "-", false);
		}
	}

	/* ---- buttons (pressed: lit and sunk a pixel) */
	for (int i = 0; i < NUM_PANEL_BUTTONS; i++) {
		const panel_button &b = Panel_buttons[i];
		int pressed = (i == Panel_pressed_button), off = pressed ? 1 : 0;
		if (pressed) gr_set_color(90, 140, 190);
		else gr_set_color(36, 56, 76);
		gr_rect(b.x, b.y + off, b.w, b.h, false);
		int w = text_w(b.label);
		gr_set_color(230, 230, 230);
		gr_string(b.x + (b.w - w) / 2, b.y + off + (b.h - 9) / 2, b.label, false);
	}

	/* the highlight (panel on the top screen, SELECT + B): a gold ring inside the target */
	if (Panel_nav) {
		int x, y, w, h;
		nav_rect(Panel_focus, &x, &y, &w, &h);
		if (Panel_focus < NUM_PANEL_BUTTONS && Panel_focus == Panel_pressed_button) y += 1;
		gr_set_color(255, 220, 120);
		gr_rect(x, y, w, 2, false);
		gr_rect(x, y + h - 2, w, 2, false);
		gr_rect(x, y, 2, h, false);
		gr_rect(x + w - 2, y, 2, h, false);
	}

	gr_reset_clip();
}

static void panel_release(void)
{
	if (Panel_pressed_key >= 0) key_mark(Panel_pressed_key, 0, 0);
	Panel_pressed_key = -1;
	Panel_pressed_button = -1;
	Panel_pressed_ets = -1;
}

void n3ds_panel_set_nav(int on)
{
	Panel_nav = on;
	if (!on && Panel_nav_hold) { Panel_nav_hold = 0; panel_release(); }
}

void n3ds_panel_nav(int dx, int dy, int press)
{
	if (Panel_nav_hold > 0) {
		/* the pressed key is held a few frames so the game sees it, then let go */
		if (--Panel_nav_hold == 0) panel_release();
		return;
	}
	if (!Panel_nav) return;
	if (dx || dy) {
		/* nearest target in that direction (distance along it, plus 2.5x the offset across) */
		int x, y, w, h;
		nav_rect(Panel_focus, &x, &y, &w, &h);
		float cx = x + w * 0.5f, cy = y + h * 0.5f, bd = 1e9f;
		int best = -1;
		for (int i = 0; i < NAV_TARGETS; i++) {
			if (i == Panel_focus) continue;
			nav_rect(i, &x, &y, &w, &h);
			float tx = x + w * 0.5f, ty = y + h * 0.5f;
			float along = dx ? (tx - cx) * dx : (ty - cy) * dy;
			float across = dx ? (ty - cy) : (tx - cx);
			if (along < 2) continue;
			float d = along + 2.5f * (across < 0 ? -across : across);
			if (d < bd) { bd = d; best = i; }
		}
		if (best >= 0) Panel_focus = best;
	}
	if (press) {
		panel_release();
		int key;
		if (Panel_focus < NUM_PANEL_BUTTONS) {
			key = Panel_buttons[Panel_focus].key;
			Panel_pressed_button = Panel_focus;
		} else {
			int e = Panel_focus - NUM_PANEL_BUTTONS;
			key = (e & 1) ? Ets_cols[e / 2].key_down : Ets_cols[e / 2].key_up;
			Panel_pressed_ets = e;
		}
		Panel_pressed_key = key;
		n3ds_click();
		key_mark(key, 1, 0);
		Panel_nav_hold = 6;
	}
}

void n3ds_panel_touch(int held, int x, int y, int pressed)
{
	static int by_touch = 0;   /* the pressed key came from the stylus (not from A) */
	if (!held) {
		if (by_touch) panel_release();
		by_touch = 0;
		return;
	}
	if (!pressed) return;   /* only react to new taps */

	panel_release();
	Panel_nav_hold = 0;
	by_touch = 1;

	for (int i = 0; i < NUM_PANEL_BUTTONS; i++) {
		const panel_button &b = Panel_buttons[i];
		if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) {
			Panel_pressed_key = b.key;
			Panel_pressed_button = i;
			n3ds_click();
			key_mark(b.key, 1, 0);
			return;
		}
	}
	for (int c = 0; c < 3; c++) {
		if (x >= Ets_cols[c].x - 1 && x < Ets_cols[c].x + ETS_W + 1 && y >= ETS_Y && y < ETS_Y + ETS_H) {
			int lower = (y >= ETS_Y + ETS_H / 2);
			int key = lower ? Ets_cols[c].key_down : Ets_cols[c].key_up;
			Panel_pressed_key = key;
			Panel_pressed_ets = c * 2 + lower;
			n3ds_click();
			key_mark(key, 1, 0);
			return;
		}
	}
}
