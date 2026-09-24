#ifndef PVZ2_CONTROLLER_H
#define PVZ2_CONTROLLER_H
#include <stdint.h>
/* All state belongs to the main/input thread. No allocation or worker GL. */
int pvz2_controller_pad(uint32_t buttons, unsigned lx, unsigned ly, unsigned rx, unsigned ry);
void pvz2_controller_release(void);
void pvz2_controller_touch(int id, int down, int x, int y);
int pvz2_controller_cursor(int *x, int *y);
int pvz2_visual_poll(void);
int pvz2_visual_active(void);
#endif
