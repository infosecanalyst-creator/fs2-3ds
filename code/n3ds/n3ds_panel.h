#ifndef _N3DS_PANEL_H
#define _N3DS_PANEL_H

/* Bottom-screen cockpit panel shown while flying (320x240, drawn by the engine). */
void n3ds_panel_draw(void);

/* touch: held = stylus down now, pressed = went down this frame */
void n3ds_panel_touch(int held, int x, int y, int pressed);

/* buttons without touch (panel on the top screen): SELECT + B turns the gold highlight on;
   call n3ds_panel_nav every flight frame with the D-Pad step and A (press = 1) */
void n3ds_panel_set_nav(int on);
void n3ds_panel_nav(int dx, int dy, int press);

/* resync the engine's OpenGL state cache after the platform layer touched GL directly */
void opengl_state_n3ds_resync(void);

#endif
