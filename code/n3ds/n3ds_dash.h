#ifndef _N3DS_DASH_H
#define _N3DS_DASH_H

/* Dashboard shown on the touchscreen while in menus (320x240). The menu itself is on
   the top screen at a readable zoom - nothing is shown on both screens. */
void n3ds_dash_draw(void);

/* touch on the dashboard (bottom screen in menus): BACK / ENTER / ZOOM / MANUAL buttons */
void n3ds_dash_touch(int held, int x, int y, int pressed);

/* index of the dashboard button at (x, y), or -1 (the rest of the dashboard is a touchpad) */
int n3ds_dash_button_at(int x, int y);

/* buttons without touch (dashboard on the top screen): on = show the gold highlight; call
   n3ds_dash_nav every menu frame with the D-Pad step and A (press = 1) */
void n3ds_dash_set_nav(int on);
void n3ds_dash_nav(int dx, int dy, int press);

/* label of the menu region under the pointer (main hall rooms etc.) for this frame */
void n3ds_dash_hover(const char *text);

/* House-style START port menu (menus and flight): RESUME / MANUAL / (GAME MENU in flight) / EXIT GAME, EXIT asks YES/NO
   with the focus on NO. Drawn over the dashboard; works by touch or D-Pad + A; B or START close. */
int  n3ds_portmenu_is_open(void);
void n3ds_portmenu_open(int in_flight);   /* in flight it has a GAME MENU row and the platform pauses the game */
void n3ds_portmenu_draw_flight(void);      /* the menu on the cockpit panel's screen */
extern int n3ds_pm_game_menu_requested;    /* GAME MENU chosen: the platform sends Esc once the menu is closed */
void n3ds_portmenu_close(void);
/* one call per menu frame while open: D-Pad step, A, B, START, and the touch (dashboard coords;
   pass held = pressed = 0 when the dashboard can't be touched) */
void n3ds_portmenu_input(int dy, int a, int b, int start, int touch_held, int tx, int ty, int touch_pressed);
/* set by EXIT GAME -> YES; the platform code saves the pilot and exits */
extern int n3ds_exit_requested;

#endif
