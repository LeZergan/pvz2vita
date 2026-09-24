/*
 * Copyright (C) 2021 Andy Nguyen
 * Copyright (C) 2021 Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/dialog.h"
#include "utils/telemetry.h"
#include "utils/boot_check.h"

#include <string.h>
#include <stdarg.h>
#include <psp2/appmgr.h>
#include <psp2/common_dialog.h>
#include <psp2/ime_dialog.h>
#include <psp2/message_dialog.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>

#ifdef USE_PVR_PSP2
#include <EGL/egl.h>
#else
#include <vitaGL.h>
#endif

static uint16_t ime_title_utf16[SCE_IME_DIALOG_MAX_TITLE_LENGTH];
static uint16_t ime_initial_text_utf16[SCE_IME_DIALOG_MAX_TEXT_LENGTH];
static uint16_t ime_input_text_utf16[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];
static uint8_t ime_input_text_utf8[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];

void _utf16_to_utf8(const uint16_t *src, uint8_t *dst) {
    for (int i = 0; src[i]; i++) {
        if ((src[i] & 0xFF80) == 0) {
            *(dst++) = src[i] & 0xFF;
        } else if ((src[i] & 0xF800) == 0) {
            *(dst++) = ((src[i] >> 6) & 0xFF) | 0xC0;
            *(dst++) = (src[i] & 0x3F) | 0x80;
        } else if ((src[i] & 0xFC00) == 0xD800 && (src[i + 1] & 0xFC00) == 0xDC00) {
            uint32_t cp = 0x10000 + ((src[i] - 0xD800) << 10) + (src[i + 1] - 0xDC00);
            *(dst++) = ((cp >> 18) & 0x07) | 0xF0;
            *(dst++) = ((cp >> 12) & 0x3F) | 0x80;
            *(dst++) = ((cp >> 6) & 0x3F) | 0x80;
            *(dst++) = (cp & 0x3F) | 0x80;
            i++;
        } else {
            *(dst++) = ((src[i] >> 12) & 0x0F) | 0xE0;
            *(dst++) = ((src[i] >> 6) & 0x3F) | 0x80;
            *(dst++) = (src[i] & 0x3F) | 0x80;
        }
    }
    *dst = '\0';
}

int init_ime_dialog(const char *title, const char *initial_text) {
    for (int i = 0; title[i]; i++) {
        ime_title_utf16[i] = title[i];
    }
    for (int i = 0; initial_text[i]; i++) {
        ime_initial_text_utf16[i] = initial_text[i];
    }

    SceImeDialogParam param;
    sceImeDialogParamInit(&param);

    param.supportedLanguages = 0x0001FFFF;
    param.languagesForced = SCE_FALSE;
    param.type = SCE_IME_TYPE_BASIC_LATIN;
    param.option = 0;
    param.title = ime_title_utf16;
    param.maxTextLength = SCE_IME_DIALOG_MAX_TEXT_LENGTH;
    param.initialText = ime_initial_text_utf16;
    param.inputTextBuffer = ime_input_text_utf16;

    return sceImeDialogInit(&param);
}

char *get_ime_dialog_result(void) {
    if (sceImeDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED)
        return NULL;
    SceImeDialogResult res;
    sceImeDialogGetResult(&res);
    if (res.button == SCE_IME_DIALOG_BUTTON_ENTER) {
        _utf16_to_utf8(ime_input_text_utf16, ime_input_text_utf8);
        sceImeDialogTerm();
        return (char *)ime_input_text_utf8;
    }
    sceImeDialogTerm();
    return NULL;
}

int init_msg_dialog(const char *msg) {
    SceMsgDialogParam param;
    sceMsgDialogParamInit(&param);
    param.mode = SCE_MSG_DIALOG_MODE_USER_MSG;
    SceMsgDialogUserMessageParam user_msg;
    memset(&user_msg, 0, sizeof(user_msg));
    user_msg.msg = (SceChar8 *)msg;
    user_msg.buttonType = SCE_MSG_DIALOG_BUTTON_TYPE_OK;
    param.userMsgParam = &user_msg;
    return sceMsgDialogInit(&param);
}

int get_msg_dialog_result(void) {
    if (sceMsgDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED)
        return 0;
    sceMsgDialogTerm();
    return 1;
}

static int graphics_ready;
void pvz2_dialog_graphics_ready(void) { graphics_ready = 1; }

void fatal_error(const char *fmt, ...) {
    va_list list;
    char string[1024];

    va_start(list, fmt);
    sceClibVsnprintf(string, sizeof(string), fmt, list);
    va_end(list);

    telemetry_log("FATAL", "%s", string);
    if (!graphics_ready) pvz2_boot_screen(string);

#ifdef USE_PVR_PSP2
    /* PVR display already initialized by gl_init(); rely on
     * the global EGL display/surface set by pvr_init_gl. */
    extern void *g_pvr_egl_display;
    extern void *g_pvr_egl_surface;
#endif

    int dialog_rc = init_msg_dialog(string);
    if (dialog_rc < 0) {
        /* A failed dialog never reaches FINISHED. Waiting for it used to loop
         * forever, including when graphics-memory recovery reports an error. */
        telemetry_log("FATAL", "error dialog unavailable rc=0x%x", (unsigned)dialog_rc);
        sceKernelExitProcess(1);
        for (;;);
    }

    while (!get_msg_dialog_result())
#ifdef USE_PVR_PSP2
        eglSwapBuffers(g_pvr_egl_display, g_pvr_egl_surface);
#else
        vglSwapBuffers(GL_TRUE);
#endif

    sceKernelExitProcess(1);
    while (1);
}
