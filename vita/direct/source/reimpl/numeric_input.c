/* User-operated Vita keyboard for both Android keyboard requests. */
#include <string.h>
#include <stdatomic.h>
#include <psp2/ime_dialog.h>
#include <psp2/sysmodule.h>
#include "java_runtime.h"
#include "reimpl/controls.h"
#include "utils/telemetry.h"
#include "utils/ime_utf8.h"

static atomic_int requested;
static atomic_int running;
static atomic_int pending_commit; /* 1 queued, 2 dispatched this frame */
static SceWChar16 output[65], initial[1];
static const SceWChar16 title[] = {'E','n','t','e','r',' ','n','u','m','b','e','r',0};
static const SceWChar16 text_title[] = {'E','n','t','e','r',' ','t','e','x','t',0};

void pvz2_numeric_request(int wanted) { atomic_store(&requested, !!wanted); }
void pvz2_text_request(void) { atomic_store(&requested, 2); }
int pvz2_numeric_active(void) { return atomic_load(&requested) || running; }

int pvz2_keyboard_is_showing(void) { return pvz2_numeric_active() || atomic_load(&pending_commit); }
int pvz2_keyboard_commit_pending(void) { return atomic_load(&pending_commit) != 0; }
void pvz2_keyboard_text_delivered(void) {
    int queued = 1;
    atomic_compare_exchange_strong(&pending_commit, &queued, 2);
}
void pvz2_keyboard_after_frame(void) {
    int delivered = 2;
    if (atomic_compare_exchange_strong(&pending_commit, &delivered, 0))
        telemetry_log("INPUT", "text dispatch frame complete");
}

int pvz2_numeric_poll(void) {
    if (atomic_load(&pending_commit)) return 1;
    if (!running && atomic_load(&requested)) {
        controls_release_for_dialog();
        sceSysmoduleLoadModule(SCE_SYSMODULE_IME);
        SceImeDialogParam param;
        sceImeDialogParamInit(&param);
        memset(output, 0, sizeof(output));
        int mode = atomic_load(&requested);
        param.type = mode == 1 ? SCE_IME_TYPE_NUMBER : SCE_IME_TYPE_DEFAULT;
        param.title = mode == 1 ? title : text_title;
        param.maxTextLength = mode == 1 ? 16 : 64;
        param.initialText = initial;
        param.inputTextBuffer = output;
        param.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
        int rc = sceImeDialogInit(&param);
        if (rc < 0) {
            controls_restore_sampling();
            atomic_store(&requested, 0);
            telemetry_log("INPUT", "keyboard could not open: 0x%x", (unsigned)rc);
            return 0;
        }
        running = mode;
        telemetry_log("INPUT", "keyboard opened mode=%s", mode == 1 ? "number" : "text");
    }
    if (!running) return 0;
    if (!atomic_load(&requested)) sceImeDialogAbort();
    if (sceImeDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_FINISHED) {
        SceImeDialogResult result = {0};
        int rc = sceImeDialogGetResult(&result);
        telemetry_log("INPUT", "IME result rc=0x%x result=%d button=%d requested=%d",
                      (unsigned)rc, result.result, result.button, atomic_load(&requested));
        if (atomic_load(&requested) && rc >= 0 && !result.result &&
            result.button == SCE_IME_DIALOG_BUTTON_ENTER) {
            char text[257];
            unsigned n = 0;
            if (running == 1) {
                for (unsigned i = 0; i < 16 && output[i]; ++i)
                    if (output[i] >= '0' && output[i] <= '9') text[n++] = (char)output[i];
                text[n] = 0;
            } else ime_utf8(output, 64, text, sizeof(text));
            atomic_store(&pending_commit, 1);
            pvz2_input_push_text(text);
            telemetry_log("INPUT", "text commit queued bytes=%u", (unsigned)strlen(text));
            memset(text, 0, sizeof(text));
        }
        sceImeDialogTerm();
        controls_restore_sampling();
        memset(output, 0, sizeof(output));
        running = 0;
        atomic_store(&requested, 0);
        telemetry_log("INPUT", "keyboard closed");
    }
    return 1; /* Also suppress the closing button/touch for this frame. */
}
