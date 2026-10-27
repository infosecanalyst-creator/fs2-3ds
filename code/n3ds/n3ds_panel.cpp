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
	{ 258, 176, 60, 30, "ATTACKR",  KEY_R },
	{   2, 208, 62, 30, "MATCH",    KEY_M },
	{  66, 208, 62, 30, "ORDERS",   KEY_C },
	{ 130, 208, 62, 30, "EQ SHLD",  KEY_Q },
	{ 194, 208, 62, 30, "BOMB",     KEY_B },
	{ 258, 208, 60, 30, "RADAR",    KEY_PADPLUS },
};
#define NUM_PANEL_BUTTONS ((int)(sizeof(Panel_buttons) / sizeof(Panel_buttons[0])))

/* energy transfer columns: shields, weapons, engines */
struct ets_column { int x; const char *label; int key_up; int key_down; };
static const ets_column Ets_cols[3] = {
	{ 196, "S", KEY_HOME,   KEY_END },
	{ 238, "W", KEY_INSERT, KEY_DELETE },
	{ 280, "E", KEY_PAGEUP, KEY_PAGEDOWN },
};
#define ETS_Y 74
#define ETS_H 96

static int Panel_pressed_key = -1;
static int Panel_pressed_button = -1;

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

void n3ds_panel_draw(void)
{
	gr_set_clip(0, 0, PANEL_W, PANEL_H, false);
	gr_set_color(8, 14, 20);
	gr_rect(0, 0, PANEL_W, PANEL_H, false);
	gr_set_font(FONT1);

	char buf[64];

	/* ---- target */
	gr_set_color(30, 50, 70);
	gr_rect(0, 0, PANEL_W, 36, false);
	if (Player_ai && Player_ai->target_objnum >= 0 && Player_obj) {
		object *tobj = &Objects[Player_ai->target_objnum];
		gr_set_color(255, 200, 80);
		if (tobj->type == OBJ_SHIP) {
			ship *tsp = &Ships[tobj->instance];
			gr_string(4, 3, tsp->ship_name, false);
			gr_set_color(200, 200, 200);
			gr_string(4, 18, Ship_info[tsp->ship_info_index].name, false);
			sprintf(buf, "HULL %d%%", (int)(get_hull_pct(tobj) * 100.0f + 0.5f));
			gr_set_color(255, 255, 255);
			gr_string(180, 3, buf, false);
		} else {
			gr_string(4, 3, "TARGET", false);
		}
		float dist = vm_vec_dist(&Player_obj->pos, &tobj->pos);
		sprintf(buf, "%dm", (int)dist);
		gr_set_color(255, 255, 255);
		gr_string(180, 18, buf, false);
	} else {
		gr_set_color(140, 140, 140);
		gr_string(4, 10, "NO TARGET", false);
	}

	/* ---- own ship */
	if (Player_obj && Player_ship) {
		gr_set_color(200, 200, 200);
		gr_string(4, 42, "HULL", false);
		panel_bar(40, 43, 70, 8, get_hull_pct(Player_obj), 80, 220, 80);
		gr_set_color(200, 200, 200);
		gr_string(4, 56, "SHLD", false);
		float smax = Ship_info[Player_ship->ship_info_index].max_shield_strength;
		float s = 0.0f;
		for (int i = 0; i < MAX_SHIELD_SECTIONS; i++) s += Player_obj->shield_quadrant[i];
		panel_bar(40, 57, 70, 8, smax > 0.0f ? s / smax : 0.0f, 80, 160, 255);

		float vmax = Player_obj->phys_info.max_vel.xyz.z;
		sprintf(buf, "SPD %d", (int)Player_obj->phys_info.fspeed);
		gr_set_color(200, 200, 200);
		gr_string(118, 42, buf, false);
		gr_string(118, 56, "THR", false);
		panel_bar(140, 57, 46, 8, Player_obj->phys_info.forward_thrust, 255, 180, 60);
		(void)vmax;

		/* afterburner fuel */
		gr_set_color(200, 200, 200);
		gr_string(4, 70, "AFTB", false);
		float abmax = Ship_info[Player_ship->ship_info_index].afterburner_fuel_capacity;
		panel_bar(40, 71, 70, 8, abmax > 0.0f ? Player_ship->afterburner_fuel / abmax : 0.0f, 255, 120, 60);

		/* weapon energy */
		gr_string(4, 84, "WPN", false);
		float wmax = Ship_info[Player_ship->ship_info_index].max_weapon_reserve;
		panel_bar(40, 85, 70, 8, wmax > 0.0f ? Player_ship->weapon_energy / wmax : 0.0f, 220, 220, 80);

		/* ---- ETS */
		int idx[3] = { Player_ship->shield_recharge_index, Player_ship->weapon_recharge_index, Player_ship->engine_recharge_index };
		for (int c = 0; c < 3; c++) {
			gr_set_color(200, 200, 200);
			gr_string(Ets_cols[c].x + 8, ETS_Y - 14, Ets_cols[c].label, false);
			panel_vbar(Ets_cols[c].x, ETS_Y, 28, ETS_H, Energy_levels[idx[c]], 120, 200, 255);
			gr_set_color(255, 255, 255);
			gr_string(Ets_cols[c].x + 10, ETS_Y + 2, "+", false);
			gr_string(Ets_cols[c].x + 10, ETS_Y + ETS_H - 12, "-", false);
		}
	}

	/* ---- buttons */
	for (int i = 0; i < NUM_PANEL_BUTTONS; i++) {
		const panel_button &b = Panel_buttons[i];
		if (i == Panel_pressed_button) gr_set_color(90, 140, 190);
		else gr_set_color(36, 56, 76);
		gr_rect(b.x, b.y, b.w, b.h, false);
		int w, h;
		gr_get_string_size(&w, &h, (char *)b.label);
		gr_set_color(230, 230, 230);
		gr_string(b.x + (b.w - w) / 2, b.y + (b.h - h) / 2, b.label, false);
	}

	gr_reset_clip();
}

static void panel_release(void)
{
	if (Panel_pressed_key >= 0) key_mark(Panel_pressed_key, 0, 0);
	Panel_pressed_key = -1;
	Panel_pressed_button = -1;
}

void n3ds_panel_touch(int held, int x, int y, int pressed)
{
	if (!held) {
		panel_release();
		return;
	}
	if (!pressed) return;   /* only react to new taps */

	panel_release();

	for (int i = 0; i < NUM_PANEL_BUTTONS; i++) {
		const panel_button &b = Panel_buttons[i];
		if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) {
			Panel_pressed_key = b.key;
			Panel_pressed_button = i;
			key_mark(b.key, 1, 0);
			return;
		}
	}
	for (int c = 0; c < 3; c++) {
		if (x >= Ets_cols[c].x - 4 && x < Ets_cols[c].x + 32 && y >= ETS_Y && y < ETS_Y + ETS_H) {
			int key = (y < ETS_Y + ETS_H / 2) ? Ets_cols[c].key_up : Ets_cols[c].key_down;
			Panel_pressed_key = key;
			key_mark(key, 1, 0);
			return;
		}
	}
}
