/*
 * Shared runtime values exported from java.c.
 */

#ifndef MCSM_JAVA_RUNTIME_H
#define MCSM_JAVA_RUNTIME_H

int mcsm_get_framebuffer_width(void);
int mcsm_get_framebuffer_height(void);
int mcsm_get_render_scale_width(void);
int mcsm_get_render_scale_height(void);

/* Vita controls -> PvZ2's AndroidGameApp.UI_ProcessEvents ByteBuffer. */
#define PVZ2_INPUT_TOUCH_DOWN 0
#define PVZ2_INPUT_TOUCH_MOVE 1
#define PVZ2_INPUT_TOUCH_UP   3
void pvz2_input_push_touch(int source_id, int phase, int x, int y);
void pvz2_input_push_key(int android_keycode, int down);
void pvz2_input_push_text(const char *text);
void pvz2_numeric_request(int wanted);
void pvz2_text_request(void);
int pvz2_keyboard_is_showing(void);
int pvz2_keyboard_commit_pending(void);
void pvz2_keyboard_text_delivered(void);
void pvz2_keyboard_after_frame(void);
int pvz2_numeric_active(void);
/* Pumps the system keyboard; returns true while game controls must be held. */
int pvz2_numeric_poll(void);

#endif
