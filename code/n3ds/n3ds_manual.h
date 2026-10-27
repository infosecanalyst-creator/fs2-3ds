#ifndef _N3DS_MANUAL_H
#define _N3DS_MANUAL_H

int  n3ds_manual_is_open(void);
void n3ds_manual_toggle(void);
void n3ds_manual_close(void);
void n3ds_manual_page(int delta);
void n3ds_manual_draw(void);   /* 320x240, drawn into the main target corner */

#endif
