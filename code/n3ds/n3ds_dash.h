#ifndef _N3DS_DASH_H
#define _N3DS_DASH_H

/* Top-screen dashboard shown while in menus (400x240, drawn by the engine into the
   corner of the main target and copied 1:1 to the top LCD). The menu itself is only on
   the touchscreen - nothing is shown on both screens. */
void n3ds_dash_draw(void);

/* label of the menu region under the pointer (main hall rooms etc.) for this frame */
void n3ds_dash_hover(const char *text);

#endif
