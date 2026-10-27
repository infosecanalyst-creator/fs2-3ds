#ifndef _N3DS_PANEL_H
#define _N3DS_PANEL_H

/* Bottom-screen cockpit panel shown while flying (320x240, drawn by the engine). */
void n3ds_panel_draw(void);

/* touch: held = stylus down now, pressed = went down this frame */
void n3ds_panel_touch(int held, int x, int y, int pressed);

/* resync the engine's OpenGL state cache after the platform layer touched GL directly */
void opengl_state_n3ds_resync(void);

#endif
