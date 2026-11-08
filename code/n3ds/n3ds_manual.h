#ifndef _N3DS_MANUAL_H
#define _N3DS_MANUAL_H

int  n3ds_manual_is_open(void);
void n3ds_manual_toggle(void);
void n3ds_manual_close(void);
void n3ds_manual_page(int delta);           /* L / R: previous / next section */
void n3ds_manual_scroll(int axis);          /* stick Y (-150..150): scroll the text */
void n3ds_manual_scroll_by(float px);       /* drag on the text (screens swapped) */
void n3ds_manual_dpad(int dx, int dy);      /* move the section highlight */
void n3ds_manual_select(void);              /* A: open the highlighted section */
void n3ds_manual_touch(int held, int x, int y, int pressed);
void n3ds_manual_draw_text(void);           /* top screen, 400x240 */
void n3ds_manual_draw(void);                /* touchscreen section buttons, 320x240 */

#endif
