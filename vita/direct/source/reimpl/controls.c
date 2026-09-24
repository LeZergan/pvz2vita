/*
 * Copyright (C) 2025 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/controls.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <psp2/ctrl.h>
#include <psp2/motion.h>
#include <psp2/touch.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>

static pthread_mutex_t g_controls_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct AnalogRuntimeState {
    int active;
    float last_x;
    float last_y;
    unsigned polls_since_emit;
} AnalogRuntimeState;

#define ANALOG_MOVE_EPSILON 0.035f
#define ANALOG_MOVE_MAX_SKIP_POLLS 2U
#define ANALOG_ACTIVE_EPSILON 0.050f

static AnalogRuntimeState g_analog_state[2];
static unsigned touch_read_errors, touch_restarts, touch_downs, touch_ups;
static int touch_last_error;
static void release_touches(void);

/* Keep the active game awake and restore sampling after a system interruption.
 * One check per second, including while IME owns input; no per-event syscalls. */
void controls_tick(uint64_t now_us) {
    static uint64_t next_check;
    static unsigned recovered_errors;
    if (now_us < next_check) return;
    next_check = now_us + UINT64_C(1000000);
    sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
    SceTouchSamplingState state;
    if (sceTouchGetSamplingState(SCE_TOUCH_PORT_FRONT, &state) >= 0 &&
        (state != SCE_TOUCH_SAMPLING_STATE_START || recovered_errors != touch_read_errors)) {
        release_touches();
        if (sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START) >= 0) {
            ++touch_restarts;
            recovered_errors = touch_read_errors;
        }
    }
}

static float clamp_float(float value, float min_value, float max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

void coord_normalize(float * x, float * y, float deadzone) {
    float magnitude = sqrtf((*x * *x) + (*y * *y));
    if (deadzone < 0.0f) {
        deadzone = 0.0f;
    } else if (deadzone >= 1.0f) {
        deadzone = 0.999f;
    }

    if (!(magnitude > deadzone)) {
        *x = 0;
        *y = 0;
        return;
    }

    const float nx = *x / magnitude;
    const float ny = *y / magnitude;

    if (magnitude > 1.0f) {
        magnitude = 1.0f;
    }

    const float multiplier = ((magnitude - deadzone) / (1.0f - deadzone));
    *x = clamp_float(nx * multiplier, -1.0f, 1.0f);
    *y = clamp_float(ny * multiplier, -1.0f, 1.0f);
}

static void analog_suppress_center_noise(float *x, float *y) {
    const float magnitude = sqrtf((*x * *x) + (*y * *y));
    if (magnitude < ANALOG_ACTIVE_EPSILON) {
        *x = 0.0f;
        *y = 0.0f;
    }
}

/* System dialogs can change controller sampling independently of button input.
 * Restore both modes after termination, before reading the next game sample. */
void controls_restore_sampling(void) {
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG_WIDE);
}

void controls_init() {
    controls_restore_sampling();
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);

    /* No motion input is consumed by this port. Leave its sensor off. */
}

void poll_touch();
void poll_pad();
void poll_accel();

void poll_stick(ControlsStickId which, float raw_x, float raw_y, float * readings_x, float * readings_y, float deadzone);

static int dialog_release_barrier;
static int controls_dialog_barrier(void);
void controls_poll() {
    if (controls_dialog_barrier()) return;
    pthread_mutex_lock(&g_controls_mutex);
    poll_touch();
    poll_pad();
    //poll_accel();
    pthread_mutex_unlock(&g_controls_mutex);
}

SceTouchData touch;
SceTouchData touch_old;

static void release_touches(void) {
    for (unsigned i = 0; i < touch_old.reportNum; ++i) {
        controls_handler_touch(touch_old.report[i].id,
            (float)touch_old.report[i].x * 0.5f, (float)touch_old.report[i].y * 0.5f,
            CONTROLS_ACTION_UP);
        ++touch_ups;
    }
    memset(&touch_old, 0, sizeof(touch_old));
}

void controls_format_stats(char *out, unsigned capacity) {
    snprintf(out, capacity, "down=%u up=%u active=%u read_errors=%u last_error=0x%x restarts=%u barrier=%d",
        touch_downs, touch_ups, (unsigned)touch_old.reportNum, touch_read_errors,
        (unsigned)touch_last_error, touch_restarts, dialog_release_barrier);
}

void poll_touch() {
    static unsigned missing_samples;
    memset(&touch, 0, sizeof(touch));
    int rc = sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
    if (rc <= 0 || touch.reportNum > SCE_TOUCH_MAX_REPORT) {
        /* A missing sample is not a finger-up, but persistent loss must not
         * leave controller ownership blocked by a stale physical capture. */
        if (rc == 0 && ++missing_samples >= 8) {
            release_touches();
            missing_samples = 8;
        }
        if (rc < 0 || touch.reportNum > SCE_TOUCH_MAX_REPORT) {
            ++touch_read_errors;
            touch_last_error = rc < 0 ? rc : -1;
            release_touches();
        }
        return; /* Never replay an old sample when the system returned none. */
    }
    missing_samples = 0;

    /* End old captures before introducing replacement contacts in this sample. */
    for (int i = 0; i < touch_old.reportNum; i++) {
        int finger_up = 1;

        for (int j = 0; j < touch.reportNum; j++) {
            if (touch.report[j].id == touch_old.report[i].id ) {
                finger_up = 0;
                break;
            }
        }

        if (finger_up == 1) {
            float x = (float) touch_old.report[i].x * 960.f / 1920.0f;
            float y = (float) touch_old.report[i].y * 544.f / 1088.0f;

            controls_handler_touch(touch_old.report[i].id, x, y, CONTROLS_ACTION_UP);
            ++touch_ups;
        }
    }

    for (int i = 0; i < touch.reportNum; i++) {
        float x = (float) touch.report[i].x * 960.f / 1920.0f;
        float y = (float) touch.report[i].y * 544.f / 1088.0f;

        // Check if the finger was down before to distinguish between the Move and Down events
        int old_index = -1;

        if (touch_old.reportNum > 0) {
            for (int j = 0; j < touch_old.reportNum; j++) {
                if (touch.report[i].id == touch_old.report[j].id) {
                    old_index = j;
                    break;
                }
            }
        }

        if (old_index < 0) {
            controls_handler_touch(touch.report[i].id, x, y, CONTROLS_ACTION_DOWN);
            ++touch_downs;
        } else {
            const float old_x = (float) touch_old.report[old_index].x * 960.f / 1920.0f;
            const float old_y = (float) touch_old.report[old_index].y * 544.f / 1088.0f;
            if (fabsf(x - old_x) >= 1.0f || fabsf(y - old_y) >= 1.0f) {
                controls_handler_touch(touch.report[i].id, x, y, CONTROLS_ACTION_MOVE);
            }
        }
    }

    sceClibMemcpy(&touch_old, &touch, sizeof(touch));
}

static ButtonMapping mapping[] = {
        { SCE_CTRL_UP,        AKEYCODE_DPAD_UP },
        { SCE_CTRL_DOWN,      AKEYCODE_DPAD_DOWN },
        { SCE_CTRL_LEFT,      AKEYCODE_DPAD_LEFT },
        { SCE_CTRL_RIGHT,     AKEYCODE_DPAD_RIGHT },
        { SCE_CTRL_CROSS,     AKEYCODE_BUTTON_A },
        { SCE_CTRL_CIRCLE,    AKEYCODE_BUTTON_B },
        { SCE_CTRL_SQUARE,    AKEYCODE_BUTTON_X },
        { SCE_CTRL_TRIANGLE,  AKEYCODE_BUTTON_Y },
        { SCE_CTRL_L1,        AKEYCODE_BUTTON_L1 },
        { SCE_CTRL_R1,        AKEYCODE_BUTTON_R1 },
        { SCE_CTRL_START,     AKEYCODE_BUTTON_START },
        { SCE_CTRL_SELECT,    AKEYCODE_BUTTON_SELECT },
};

uint32_t old_buttons = 0, current_buttons = 0, pressed_buttons = 0, released_buttons = 0;
static int pad_owned;

float analog_lx[3] = { 0 };
float analog_ly[3] = { 0 };
float analog_rx[3] = { 0 };
float analog_ry[3] = { 0 };

void poll_pad() {
    SceCtrlData pad = {0};
    int rc = sceCtrlPeekBufferPositiveExt2(0, &pad, 1);
    if (rc <= 0) {
        if (rc < 0) controls_handler_reset();
        return;
    }

    // Gamepad buttons
    old_buttons = current_buttons;
    current_buttons = pad.buttons;
    pressed_buttons = current_buttons & ~old_buttons;
    released_buttons = ~current_buttons & old_buttons;

    pad_owned = controls_handler_pad(pad.buttons, pad.lx, pad.ly, pad.rx, pad.ry);
    if (pad_owned) return;

    for (int i = 0; i < sizeof(mapping) / sizeof(ButtonMapping); i++) {
        if (pressed_buttons & mapping[i].sce_button) {
            controls_handler_key(mapping[i].android_button, CONTROLS_ACTION_DOWN);
        }
        if (released_buttons & mapping[i].sce_button) {
            controls_handler_key(mapping[i].android_button, CONTROLS_ACTION_UP);
        }
    }

    // Analog sticks
    poll_stick(CONTROLS_STICK_LEFT, (float)pad.lx, (float)pad.ly, analog_lx, analog_ly, LEFT_ANALOG_DEADZONE);
    poll_stick(CONTROLS_STICK_RIGHT, (float)pad.rx, (float)pad.ry, analog_rx, analog_ry, RIGHT_ANALOG_DEADZONE);
}

void poll_stick(ControlsStickId which, float raw_x, float raw_y, float * readings_x, float * readings_y, float deadzone) {
    readings_x[0] = (raw_x - 128.0f) / 127.0f;
    readings_y[0] = (raw_y - 128.0f) / 127.0f;

    coord_normalize(&readings_x[0], &readings_y[0], deadzone);
    analog_suppress_center_noise(&readings_x[0], &readings_y[0]);

    const unsigned state_index = (which == CONTROLS_STICK_RIGHT) ? 1U : 0U;
    AnalogRuntimeState *state = &g_analog_state[state_index];
    const float x = readings_x[0];
    const float y = readings_y[0];
    const int centered = (x == 0.0f && y == 0.0f);

    if (centered) {
        if (state->active) {
            controls_handler_analog(which, 0.0f, 0.0f, CONTROLS_ACTION_UP);
        }
        state->active = 0;
        state->last_x = 0.0f;
        state->last_y = 0.0f;
        state->polls_since_emit = 0;
    } else if (!state->active) {
        controls_handler_analog(which, x, y, CONTROLS_ACTION_DOWN);
        state->active = 1;
        state->last_x = x;
        state->last_y = y;
        state->polls_since_emit = 0;
    } else {
        state->polls_since_emit++;
        if (fabsf(x - state->last_x) >= ANALOG_MOVE_EPSILON ||
            fabsf(y - state->last_y) >= ANALOG_MOVE_EPSILON ||
            state->polls_since_emit >= ANALOG_MOVE_MAX_SKIP_POLLS) {
            controls_handler_analog(which, x, y, CONTROLS_ACTION_MOVE);
            state->last_x = x;
            state->last_y = y;
            state->polls_since_emit = 0;
        }
    }

    readings_x[2] = readings_x[1];
    readings_y[2] = readings_y[1];
    readings_x[1] = readings_x[0];
    readings_y[1] = readings_y[0];
}

/* Finish the gesture that opened IME, then keep IME touches/buttons out of the
 * game until released. Otherwise closing the dialog replays a stale touch-up
 * or confirmation key against the newly focused text field. */
void controls_release_for_dialog(void) {
    release_touches();
    controls_handler_reset();
    for (unsigned i = 0; i < sizeof(mapping)/sizeof(mapping[0]); ++i)
        if (!pad_owned && (current_buttons & mapping[i].sce_button))
            controls_handler_key(mapping[i].android_button, CONTROLS_ACTION_UP);
    memset(&touch_old, 0, sizeof(touch_old));
    old_buttons = current_buttons = 0;
    pad_owned = 0;
    dialog_release_barrier = 1;
}

/* Other loader targets/host fixtures keep their existing key mapping. */
__attribute__((weak)) int controls_handler_pad(uint32_t b, unsigned lx, unsigned ly, unsigned rx, unsigned ry) {
    (void)b; (void)lx; (void)ly; (void)rx; (void)ry; return 0;
}
__attribute__((weak)) void controls_handler_reset(void) {}
static int controls_dialog_barrier(void) {
    if (!dialog_release_barrier) return 0;
    SceTouchData t = {0}; SceCtrlData p = {0};
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &t, 1) <= 0 ||
        sceCtrlPeekBufferPositiveExt2(0, &p, 1) <= 0 ||
        t.reportNum > SCE_TOUCH_MAX_REPORT) return 1;
    uint32_t buttons = 0;
    for (unsigned i = 0; i < sizeof(mapping)/sizeof(mapping[0]); ++i)
        buttons |= mapping[i].sce_button;
    if (!t.reportNum) {
        /* Keep a held IME-confirm button out of the game, but allow pointer
         * movement immediately. The barrier consumes the release sample. */
        controls_handler_pad(0, p.lx, p.ly, p.rx, p.ry);
        if (!(p.buttons & buttons)) dialog_release_barrier = 0;
    }
    return 1;
}
