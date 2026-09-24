#include "controller.h"
#include "../java_runtime.h"
#include "../reimpl/controls.h"
#include "telemetry.h"
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/message_dialog.h>
#include <stdio.h>
#include <string.h>

#define SETTINGS_CHORD (SCE_CTRL_DOWN | SCE_CTRL_CROSS | SCE_CTRL_L1 | SCE_CTRL_R1)
#define POINTER_ID 254
static uint32_t previous;
static float cursor_x = 480, cursor_y = 272, return_x = 480, return_y = 272;
static uint64_t last_poll, last_active;
static int visible, held, barrier, seed = -1, visual_requested, visual_running;
static unsigned char fingers[256];
static unsigned finger_count;
static uint32_t game_keys;
static int remembered_seed;

static void pointer_up(void) {
    if (held) pvz2_input_push_touch(POINTER_ID, PVZ2_INPUT_TOUCH_UP, (int)cursor_x, (int)cursor_y);
    held = 0;
}
static void keys_up(void) {
    if (game_keys & SCE_CTRL_START) pvz2_input_push_key(82, 0);
    if (game_keys & SCE_CTRL_CIRCLE) pvz2_input_push_key(4, 0);
    game_keys = 0;
}
void pvz2_controller_release(void) {
    pointer_up();
    keys_up();
    seed = -1;
    previous = 0; barrier = 1; last_poll = 0;
}
void pvz2_controller_touch(int id, int down, int x, int y) {
    if ((unsigned)id >= 254) return;
    if (down && !fingers[id]) { fingers[id] = 1; ++finger_count; }
    if (!down && fingers[id]) { fingers[id] = 0; --finger_count; }
    pointer_up(); keys_up(); visible = 0; seed = -1;
    cursor_x = x < 0 ? 0 : (x > 959 ? 959 : x);
    cursor_y = y < 0 ? 0 : (y > 543 ? 543 : y);
    barrier = 1;
}
static float axis(unsigned raw) {
    int d = (int)raw - 128;
    if (d > 24) return (d - 24) / 103.0f;
    if (d < -24) return (d + 24) / 104.0f;
    return 0;
}
int pvz2_controller_cursor(int *x, int *y) {
    *x = (int)cursor_x; *y = (int)cursor_y;
    return visible &&
           !visual_running && !visual_requested && !finger_count && !pvz2_keyboard_is_showing();
}
int pvz2_controller_pad(uint32_t buttons, unsigned lx, unsigned ly, unsigned rx, unsigned ry) {
    uint64_t now = sceKernelGetSystemTimeWide();
    float dt = last_poll && now >= last_poll ? (now - last_poll) / 1000000.0f : 0;
    last_poll = now;
    if (dt > 0.05f) dt = 0.05f; /* A load must not fling the pointer. */
    if (finger_count) {
        previous = buttons;
        return 1;
    }
    /* A takeover releases captures. Block held buttons until release, but
     * never require centered sticks to regain pointer movement. */
    if (barrier) {
        if (!buttons) barrier = 0;
        buttons = 0;
        previous = 0;
    }
    uint32_t pressed = buttons & ~previous;
    uint32_t released = previous & ~buttons;
    if (pressed) last_active = now;
    previous = buttons;
    if ((buttons & SETTINGS_CHORD) == SETTINGS_CHORD) {
        pointer_up(); keys_up(); visual_requested = 1; barrier = 1;
        return 1;
    }
    /* L+R reserves the settings chord; never send Cross into the game while
     * completing it. Release any older pointer capture first. */
    if ((buttons & (SCE_CTRL_L1|SCE_CTRL_R1)) == (SCE_CTRL_L1|SCE_CTRL_R1)) {
        pointer_up(); keys_up(); return 1;
    }
    if (visual_running || visual_requested) return 1;
    if (pressed & SCE_CTRL_START) { pvz2_input_push_key(82, 1); game_keys |= SCE_CTRL_START; }
    if ((released & SCE_CTRL_START) && (game_keys & SCE_CTRL_START)) { pvz2_input_push_key(82, 0); game_keys &= ~SCE_CTRL_START; }
    if (pressed & SCE_CTRL_CIRCLE) {
        pointer_up();
        if (seed >= 0) { cursor_x = return_x; cursor_y = return_y; seed = -1; }
        else { pvz2_input_push_key(4, 1); game_keys |= SCE_CTRL_CIRCLE; }
    }
    if ((released & SCE_CTRL_CIRCLE) && (game_keys & SCE_CTRL_CIRCLE)) { pvz2_input_push_key(4, 0); game_keys &= ~SCE_CTRL_CIRCLE; }
    if (!held && (pressed & (SCE_CTRL_L1|SCE_CTRL_R1))) {
        if (seed < 0) { return_x = cursor_x; return_y = cursor_y; seed = remembered_seed; }
        else seed = (seed + ((pressed & SCE_CTRL_R1) ? 1 : 7)) % 8;
        remembered_seed = seed;
        cursor_x = 45; cursor_y = 86 + seed * 54; visible = 1;
    }
    float dx = axis(lx) * 650 + axis(rx) * 200;
    float dy = axis(ly) * 650 + axis(ry) * 200;
    int dpad_x = (!!(buttons&SCE_CTRL_RIGHT)) - (!!(buttons&SCE_CTRL_LEFT));
    int dpad_y = (!!(buttons&SCE_CTRL_DOWN)) - (!!(buttons&SCE_CTRL_UP));
    float dpad_speed = dpad_x && dpad_y ? 226.27417f : 320.0f;
    dx += dpad_x * dpad_speed;
    dy += dpad_y * dpad_speed;
    if (dx || dy) {
        last_active = now;
        seed = -1; visible = 1;
        cursor_x += dx * dt; cursor_y += dy * dt;
        if (cursor_x < 0) cursor_x = 0; if (cursor_x > 959) cursor_x = 959;
        if (cursor_y < 0) cursor_y = 0; if (cursor_y > 543) cursor_y = 543;
    }
    if (pressed & SCE_CTRL_CROSS) {
        visible = 1; held = 1;
        pvz2_input_push_touch(POINTER_ID, PVZ2_INPUT_TOUCH_DOWN, (int)cursor_x, (int)cursor_y);
    } else if (held && (dx || dy)) {
        pvz2_input_push_touch(POINTER_ID, PVZ2_INPUT_TOUCH_MOVE, (int)cursor_x, (int)cursor_y);
    }
    if (released & SCE_CTRL_CROSS) {
        pointer_up();
        if (seed >= 0) { cursor_x = return_x; cursor_y = return_y; seed = -1; }
    }
    return 1;
}
int pvz2_visual_active(void) { return visual_running; }
int pvz2_visual_poll(void) {
    static SceMsgDialogUserMessageParam user;
    static char message[512];
    if (visual_requested && !visual_running) {
        visual_requested = 0;
        controls_release_for_dialog();
        memset(&user, 0, sizeof(user));
        snprintf(message, sizeof(message), "CONTROLS\nFixed 30 FPS\n\nLeft stick / D-pad: move pointer\nRight stick: precise pointer\nCross: press / hold to drag\nL / R: select seed slot, Cross to confirm\nCircle: back / leave seed selection\nSTART: pause\n\nPause before opening this guide during a level.");
        user.msg = message; user.buttonType = SCE_MSG_DIALOG_BUTTON_TYPE_OK;
        SceMsgDialogParam param; sceMsgDialogParamInit(&param);
        param.mode = SCE_MSG_DIALOG_MODE_USER_MSG; param.userMsgParam = &user;
        int rc = sceMsgDialogInit(&param);
        if (rc < 0) { controls_restore_sampling(); telemetry_log("INPUT", "controls guide unavailable: 0x%x", (unsigned)rc); return 1; }
        visual_running = 1;
    }
    if (!visual_running) return 0;
    if (sceMsgDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_FINISHED) {
        sceMsgDialogTerm(); visual_running = 0;
        controls_restore_sampling();
        controls_release_for_dialog();
    }
    return 1;
}
