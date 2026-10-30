#ifndef _N3DS_DASH_H
#define _N3DS_DASH_H

/* Dashboard shown on the touchscreen while in menus (320x240). The menu itself is on
   the top screen at a readable zoom - nothing is shown on both screens. */
void n3ds_dash_draw(void);

/* touch on the dashboard (bottom screen in menus): BACK / ENTER / ZOOM / MANUAL buttons */
void n3ds_dash_touch(int held, int x, int y, int pressed);

/* label of the menu region under the pointer (main hall rooms etc.) for this frame */
void n3ds_dash_hover(const char *text);

#endif
