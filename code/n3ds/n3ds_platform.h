/*
 * FreeSpace 2 for the New Nintendo 3DS - platform layer
 *
 * Screen layout:
 *   Engine renders into one 800x480 offscreen target (picaGL main target).
 *   In flight : top screen    = 3D view + HUD (target downscaled 2x, filtered)
 *               bottom screen = touch cockpit panel (targets, energy, orders, throttle)
 *   In menus  : bottom screen = the whole menu (stylus works as the mouse)
 *               top screen    = 1:1 magnifier around the stylus so text stays readable
 */
#ifndef _N3DS_PLATFORM_H
#define _N3DS_PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

/* engine render size (logical, landscape) */
#define N3DS_GAME_W 800
#define N3DS_GAME_H 480

void n3ds_init(void);
void n3ds_shutdown(void);

/* called from os_poll(): reads buttons/touch, feeds key/mouse/joystick state */
void n3ds_input_poll(void);

/* called instead of a buffer swap: composes both screens and presents */
void n3ds_present(void);

/* touch position in engine coordinates (valid while touching / last touch) */
void n3ds_mouse_pos(int *x, int *y);
int  n3ds_mouse_buttons(void);

/* virtual joystick (SDL shim reads these) */
int  n3ds_joy_axis(int axis);     /* -32768..32767 */
int  n3ds_joy_button(int button); /* 0/1 */
int  n3ds_joy_hat(void);

/* true when the app should quit (HOME menu close, power button) */
int  n3ds_should_quit(void);

/* simple debug log on the SD card (sdmc:/3ds/freespace2/fs2_3ds.log) */
void n3ds_log(const char *fmt, ...);

/* log the message, show it in the system error dialog, then return (caller exits) */
void n3ds_fatal(const char *msg);

#ifdef __cplusplus
}
#endif

#endif
