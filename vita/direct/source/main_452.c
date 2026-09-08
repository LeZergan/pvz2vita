/*
 * PvZ2 4.5.2 ROW (versionCode 147) direct ARMv7 loader for PS Vita.
 *
 * The lifecycle and addresses in this file are intentionally derived from the
 * upstream PvZ2Native 4.5.2 version table and engine/ implementation.  Vita and
 * Android are both ARMv7 here, so the game code runs directly; only the Android
 * ABI, JNI, GLES and OpenSL surface is bridged.
 */

#include <stdint.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>
#include <kubridge.h>

#include "reimpl/controls.h"
#include "reimpl/bionic_time.h"
#include "reimpl/math_softfp.h"
#include "reimpl/asset_manager.h"
#include "java_runtime.h"
#include "utils/glutil.h"
#include "utils/init.h"
#include "utils/telemetry.h"
#include "utils/pixel_workers.h"
#include "utils/text_field_452.h"
#include "utils/boot_check.h"
#include "utils/dialog.h"
#include "reimpl/rsb_index_vita.h"

#define PVZ2_PATH GAME_DATA_PATH "libPVZ2.so"

/* PvZ2Native src/game/symbols.cpp, 4.5.2 entry. */
#define OFF_GAME_APP_INITIALIZE                0x00cc033cu
#define OFF_APPLICATION_WILL_FINISH_LAUNCHING  0x00cc131cu
#define OFF_APPLICATION_DID_FINISH_LAUNCHING   0x00cc1420u
#define OFF_APPLICATION_WILL_BECOME_FOREGROUND 0x00cc142cu
#define OFF_APPLICATION_DID_BECOME_ACTIVE      0x00cc1430u
#define OFF_PUMP_MESSAGE_QUEUE                  0x00cc7cd0u
#define OFF_ON_SURFACE_CREATED                  0x00cc7cf0u
#define OFF_ON_SURFACE_CHANGED                  0x00cc7d8cu
#define OFF_ON_DRAW_FRAME                       0x00cc7e60u
#define OFF_APP_DRIVER                          0x0117a734u
#define OFF_SURFACE_CREATED_PREP                0x00ca154cu
#define OFF_SURFACE_CREATED_FINALIZE            0x00cb9cf0u
#define OFF_SURFACE_CREATED_ONCE_FLAG           0x0117a730u

#define FP_GAME_APP_INITIALIZE UINT64_C(0xE24DD084E92D4FF0)
#define FP_ON_DRAW_FRAME        UINT64_C(0xE59F1010E59F0010)

/* 4.5.2 needs far less than the newer Android releases.  Keep enough user RAM
 * available for vitaGL/CDRAM staging so this remains viable on physical Vita. */
int _newlib_heap_size_user = 160 * 1024 * 1024;
int sceLibcHeapSize = 4 * 1024 * 1024;

so_module so_mod_libcpp;
so_module so_mod_nimble;
so_module so_mod_pvz2;

/* .ARM.exidx from the exact 4.5.2 libPVZ2.so supplied with versionCode 147. */
#define PVZ2_EXIDX_VADDR 0x00fc3384u
#define PVZ2_EXIDX_COUNT (0x0004b688u / 8u)
extern const uint32_t __exidx_start[] __attribute__((weak));
extern const uint32_t __exidx_end[] __attribute__((weak));

uintptr_t __gnu_Unwind_Find_exidx(uintptr_t pc, int *count) {
    if (so_mod_pvz2.text_base && pc >= so_mod_pvz2.text_base &&
        pc < so_mod_pvz2.text_base + so_mod_pvz2.text_size) {
        if (count) *count = (int)PVZ2_EXIDX_COUNT;
        return so_mod_pvz2.text_base + PVZ2_EXIDX_VADDR;
    }
    if (count) *count = (int)((__exidx_end - __exidx_start) / 2);
    return (uintptr_t)__exidx_start;
}

static void clocks_60fps(void) {
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
    telemetry_log("60FPS", "clocks arm=%d gpu=%d bus=%d; vblank presentation enabled",
                  scePowerGetArmClockFrequency(), scePowerGetGpuClockFrequency(),
                  scePowerGetBusClockFrequency());
}

static void log_memory_stage(const char *stage) {
    SceKernelFreeMemorySizeInfo info = {0};
    info.size = sizeof(info);
    const int rc = sceKernelGetFreeMemorySize(&info);
    telemetry_log("MEM", "%s rc=0x%08x user=%u KiB cdram=%u KiB phycont=%u KiB",
                  stage, (unsigned)rc, (unsigned)(info.size_user / 1024),
                  (unsigned)(info.size_cdram / 1024), (unsigned)(info.size_phycont / 1024));
}

static void *trace_bridge(uintptr_t offset, uint32_t first, uint32_t second, void *wrapper) {
    const uintptr_t entry = so_mod_pvz2.text_base + offset;
    /* Stable trampoline: these two verified instructions are PC independent.
     * Never unpatch code while game worker threads are running. */
    if (*(uint32_t *)entry != first || *(uint32_t *)(entry + 4) != second)
        return NULL;
    extern uintptr_t so_alloc_arena(so_module *, uintptr_t, uintptr_t, size_t);
    const uintptr_t bridge = so_alloc_arena(&so_mod_pvz2, 0xffffffffu, entry, 16);
    if (!bridge) return NULL;
    const uint32_t code[] = {first, second, 0xe51ff004, entry + 8};
    kuKernelCpuUnrestrictedMemcpy((void *)bridge, code, sizeof(code));
    hook_arm(entry, (uintptr_t)wrapper);
    return (void *)bridge;
}

static int (*resource_error_original)(void *, const void *);
static int resource_error_trace(void *manager, const void *message) {
    const unsigned char *s = message;
    const char *text = (s[0] & 1) ? *(const char **)(s + 8) : (const char *)(s + 1);
    static unsigned count;
    if (count++ < 24) telemetry_log("RESOURCE_ERROR", "%.240s", text);
    return resource_error_original(manager, message);
}
static void (*text_dispatch_original)(void *, int, const void *);
static void text_dispatch_trace(void *manager, int action, const void *text) {
    /* Full-screen Vita IME edits a complete value. Select the previous value
     * for this verified EditWidget before the native validated replacement. */
    void *focused = *(void **)((char *)manager + 0x7c);
    uintptr_t handler = focused ? *(uintptr_t *)(*(uintptr_t *)focused + 0xf4) : 0;
    uintptr_t base = so_mod_pvz2.text_base;
    int edit = handler == base + 0xb1f7a8;
    unsigned before = edit ? text452_length((char *)focused + 0x88) : 0;
    unsigned submitted = text452_length(text);
    int replaced = 0;
    if (edit && action == 0 && pvz2_keyboard_commit_pending()) {
        replaced = text452_select_all(focused);
        if (replaced && !submitted) action = 3; /* Native delete-selection for empty confirmation. */
    }
    static unsigned reports;
    int report = reports++ < 32;
    if (report) {
        telemetry_log("INPUT", "native text dispatch action=%d focused=%d handler_offset=0x%x showing=%d",
                      action, focused != NULL,
                      (unsigned)((handler >= base && handler < base + so_mod_pvz2.text_size) ? handler - base : 0),
                      pvz2_keyboard_is_showing());
    }
    text_dispatch_original(manager, action, text);
    if (report && edit && *(void **)((char *)manager + 0x7c) == focused)
        telemetry_log("INPUT", "native_chars=%u replace=%d field_chars_before=%u after=%u matches_submitted=%d",
                      submitted, replaced, before, text452_length((char *)focused + 0x88),
                      text452_equal((char *)focused + 0x88, text));
}
static void install_resource_error_logging(void) {
    resource_error_original = trace_bridge(0xb786b0, 0xe92d4070, 0xe1a06001, resource_error_trace);
    text_dispatch_original = trace_bridge(0xcb7618, 0xe92d47f0, 0xe24dd010, text_dispatch_trace);
    telemetry_log("INPUT", "native text dispatch trace=%s", text_dispatch_original ? "installed" : "fingerprint mismatch");
}

static int load_exact_452(void) {
    static const uintptr_t bases[] = {0x98000000u, 0x9c000000u, 0xa0000000u};
    int result = -1;
    for (unsigned i = 0; i < sizeof(bases) / sizeof(bases[0]); ++i) {
        memset(&so_mod_pvz2, 0, sizeof(so_mod_pvz2));
        result = so_file_load(&so_mod_pvz2, PVZ2_PATH, bases[i]);
        telemetry_log("LOAD", "libPVZ2 base candidate 0x%x -> 0x%08x (%d)",
                      (unsigned)bases[i], (unsigned)result, result);
        if (result == 0) break;
        /* A different virtual address cannot replenish physical memory. */
        if ((uint32_t)result == 0x80024302u) break;
    }
    if (result != 0) return result;

    const uint64_t init_fp = *(const uint64_t *)(so_mod_pvz2.text_base + OFF_GAME_APP_INITIALIZE);
    const uint64_t draw_fp = *(const uint64_t *)(so_mod_pvz2.text_base + OFF_ON_DRAW_FRAME);
    if (init_fp != FP_GAME_APP_INITIALIZE || draw_fp != FP_ON_DRAW_FRAME) {
        telemetry_log("FATAL", "libPVZ2.so is not supported 4.5.2 (fingerprint mismatch)");
        return -2;
    }
    telemetry_log("LOAD", "exact PvZ2 4.5.2 fingerprints verified");

    result = so_relocate(&so_mod_pvz2);
    if (result != 0) return result;
    resolve_imports(&so_mod_pvz2);
    extern int placement452_install(so_module *);
    if (placement452_install(&so_mod_pvz2) != 0) {
        telemetry_log("FATAL", "4.5.2 placement grid patch fingerprint mismatch");
        return -2;
    }
    install_resource_error_logging();
    so_flush_caches(&so_mod_pvz2);
    return 0;
}

static void *native_or_offset(const char *name, uintptr_t offset) {
    void *fn = falso_jni_get_native(name);
    if (!fn) fn = (void *)(so_mod_pvz2.text_base + offset);
    return fn;
}

static void run_ea_startup(void) {
    typedef void (*JniVoid)(JNIEnv *, jclass);
    typedef void (*EAIOStartup)(JNIEnv *, jclass, jobject, jstring, jstring, jstring);
    JniVoid thread_init = (JniVoid)so_symbol(&so_mod_pvz2,
        "Java_com_ea_EAThread_EAThread_Init");
    EAIOStartup io_start = (EAIOStartup)so_symbol(&so_mod_pvz2,
        "Java_com_ea_EAIO_EAIO_StartupNativeImpl");
    JniVoid io_stop = (JniVoid)so_symbol(&so_mod_pvz2,
        "Java_com_ea_EAIO_EAIO_Shutdown");
    jstring documents = jni->NewStringUTF(&jni, DATA_PATH "documents");
    jstring files = jni->NewStringUTF(&jni, DATA_PATH);
    jstring external = jni->NewStringUTF(&jni, DATA_PATH);
    if (thread_init) thread_init(&jni, (jclass)1);
    /* 4.5.2's export at 0xdd557c forwards r2 (AssetManager), r3 and TWO
     * stack arguments to 0xdd338c, which converts all three path strings.
     * Omitting those arguments fed ARM instructions to GetStringUTFChars. */
    if (io_start) io_start(&jni, (jclass)1, (jobject)AAssetManager_create(),
                           files, documents, external);
    if (io_stop) io_stop(&jni, (jclass)1);
    telemetry_log("LIFECYCLE", "EAThread/EAIO startup sequence complete");
}

static void run_init_array_logged(void) {
    telemetry_log("INIT", "running %d constructors", so_mod_pvz2.num_init_array);
    for (int i = 0; i < so_mod_pvz2.num_init_array; ++i) {
        int (*ctor)(void) = so_mod_pvz2.init_array[i];
        if (!ctor || (intptr_t)ctor == -1) continue;
        if ((i % 25) == 0 || i == 1006) {
            telemetry_log("INIT", "constructor %d/%d offset=0x%x", i,
                          so_mod_pvz2.num_init_array,
                          (unsigned)((uintptr_t)ctor - so_mod_pvz2.text_base));
        }
        ctor();
    }
    telemetry_log("INIT", "all constructors complete");
}

static int run_game_initialize(void) {
    typedef jboolean (*GameInit)(JNIEnv *, jobject, jobject, jobject, jobject,
                                 jobject, jobject, jobject, jobject, jobject);
    static const char *classes[] = {
        "AndroidGameApp", "AndroidSurfaceView", "AndroidHttpProxy",
        "AndroidFacebookDriver", "Cloud", "GooglePlayConnect",
        "GooglePlayAchievements", "GooglePlayLeaderboard", "AndroidNotification"
    };
    jobject obj[9];
    for (unsigned i = 0; i < 9; ++i) obj[i] = jni->NewStringUTF(&jni, classes[i]);
    GameInit fn = (GameInit)native_or_offset("Native_GameAppInitialize",
                                             OFF_GAME_APP_INITIALIZE);
    telemetry_log("LIFECYCLE", "-> Native_GameAppInitialize (thiz + 8 exact 4.5.2 objects)");
    jboolean ok = fn(&jni, obj[0], obj[1], obj[2], obj[3], obj[4], obj[5],
                     obj[6], obj[7], obj[8]);
    telemetry_log("LIFECYCLE", "<- Native_GameAppInitialize = %d", (int)ok);
    return ok == JNI_TRUE ? 0 : -1;
}

static void run_lifecycle(void) {
    typedef void (*WillFinish)(JNIEnv *, jobject, jstring);
    typedef void (*VoidNative)(JNIEnv *, jobject);
    typedef void (*SurfaceChanged)(JNIEnv *, jclass, jint, jint);

    WillFinish will_finish = (WillFinish)native_or_offset(
        "Native_applicationWillFinishLaunching", OFF_APPLICATION_WILL_FINISH_LAUNCHING);
    VoidNative did_finish = (VoidNative)native_or_offset(
        "Native_applicationDidFinishLaunching", OFF_APPLICATION_DID_FINISH_LAUNCHING);
    VoidNative foreground = (VoidNative)native_or_offset(
        "Native_applicationWillBecomeForeground", OFF_APPLICATION_WILL_BECOME_FOREGROUND);
    VoidNative surface_created = (VoidNative)native_or_offset(
        "Native_onSurfaceCreated", OFF_ON_SURFACE_CREATED);
    SurfaceChanged surface_changed = (SurfaceChanged)native_or_offset(
        "Native_onSurfaceChanged", OFF_ON_SURFACE_CHANGED);
    VoidNative active = (VoidNative)native_or_offset(
        "Native_applicationDidBecomeActive", OFF_APPLICATION_DID_BECOME_ACTIVE);

    telemetry_log("LIFECYCLE", "-> Native_applicationWillFinishLaunching");
    will_finish(&jni, NULL, NULL);
    telemetry_log("LIFECYCLE", "<- Native_applicationWillFinishLaunching");
    uintptr_t driver = *(uintptr_t *)(so_mod_pvz2.text_base + OFF_APP_DRIVER);
    telemetry_log("LIFECYCLE", "4.5.2 app_driver global=0x%x", (unsigned)driver);
    if (driver) {
        telemetry_log("LIFECYCLE", "app_driver vtable=0x%x surface=0x%x",
                      (unsigned)*(uintptr_t *)driver,
                      (unsigned)*(uintptr_t *)(driver + 0x44));
    }
    telemetry_log("LIFECYCLE", "-> Native_applicationDidFinishLaunching");
    did_finish(&jni, NULL);
    telemetry_log("LIFECYCLE", "<- Native_applicationDidFinishLaunching");
    telemetry_log("LIFECYCLE", "-> Native_applicationWillBecomeForeground");
    foreground(&jni, NULL);
    telemetry_log("LIFECYCLE", "<- Native_applicationWillBecomeForeground");
    /* Diagnostic transcription of 4.5.2 Native_onSurfaceCreated at 0xcc7cf0.
     * Keeping each original call boundary visible is essential on Vita3K,
     * whose host access violation otherwise loses the guest PC. */
    (void)surface_created;
    typedef void (*ObjectCall)(void *);
    void *driver_obj = (void *)*(uintptr_t *)(so_mod_pvz2.text_base + OFF_APP_DRIVER);
    void *surface_obj = driver_obj ? (void *)*(uintptr_t *)((uintptr_t)driver_obj + 0x44) : NULL;
    telemetry_log("LIFECYCLE", "-> Native_onSurfaceCreated prep surface=0x%x vtable=0x%x",
                  (unsigned)(uintptr_t)surface_obj,
                  surface_obj ? (unsigned)*(uintptr_t *)surface_obj : 0);
    ((ObjectCall)(so_mod_pvz2.text_base + OFF_SURFACE_CREATED_PREP))(surface_obj);
    telemetry_log("LIFECYCLE", "<- Native_onSurfaceCreated prep");
    uint8_t *once = (uint8_t *)(so_mod_pvz2.text_base + OFF_SURFACE_CREATED_ONCE_FLAG);
    if (*once == 0) {
        uintptr_t driver_vt = *(uintptr_t *)driver_obj;
        ObjectCall driver_once = *(ObjectCall *)(driver_vt + 0x144);
        telemetry_log("LIFECYCLE", "-> surface driver once fn=0x%x", (unsigned)(uintptr_t)driver_once);
        driver_once(driver_obj);
        telemetry_log("LIFECYCLE", "<- surface driver once");
        *once = 1;
        uintptr_t surface_vt = *(uintptr_t *)surface_obj;
        ObjectCall surface_d4 = *(ObjectCall *)(surface_vt + 0xd4);
        ObjectCall surface_d0 = *(ObjectCall *)(surface_vt + 0xd0);
        telemetry_log("LIFECYCLE", "-> surface vfunc d4=0x%x", (unsigned)(uintptr_t)surface_d4);
        surface_d4(surface_obj);
        telemetry_log("LIFECYCLE", "<- surface vfunc d4");
        telemetry_log("LIFECYCLE", "-> surface vfunc d0=0x%x", (unsigned)(uintptr_t)surface_d0);
        surface_d0(surface_obj);
        telemetry_log("LIFECYCLE", "<- surface vfunc d0");
    }
    telemetry_log("LIFECYCLE", "-> Native_onSurfaceCreated finalize");
    ((ObjectCall)(so_mod_pvz2.text_base + OFF_SURFACE_CREATED_FINALIZE))(driver_obj);
    telemetry_log("LIFECYCLE", "<- Native_onSurfaceCreated finalize");
    telemetry_log("LIFECYCLE", "-> Native_onSurfaceChanged(960, 544)");
    surface_changed(&jni, (jclass)1, 960, 544);
    telemetry_log("LIFECYCLE", "<- Native_onSurfaceChanged");
    telemetry_log("LIFECYCLE", "-> Native_applicationDidBecomeActive");
    active(&jni, NULL);
    telemetry_log("LIFECYCLE", "<- Native_applicationDidBecomeActive");
    telemetry_log("LIFECYCLE", "GitHub 4.5.2 launch/surface/active order complete");
}

static void run_60fps_loop(void) {
    typedef void (*FrameNative)(JNIEnv *, jclass);
    FrameNative pump = (FrameNative)native_or_offset("Native_PumpMessageQueue",
                                                      OFF_PUMP_MESSAGE_QUEUE);
    FrameNative draw = (FrameNative)native_or_offset("Native_onDrawFrame",
                                                      OFF_ON_DRAW_FRAME);
    uint64_t sample_start = sceKernelGetSystemTimeWide();
    unsigned frame = 0;
    uint64_t pump_us = 0, draw_us = 0, present_us = 0;
    uint32_t peak_us = 0, slow_frames = 0;
    telemetry_log("60FPS", "frame loop start: pump -> draw -> vblank swap, target 60 Hz");
    for (;;) {
        const uint64_t begin = sceKernelGetSystemTimeWide();
        controls_tick(begin);
        if (!pvz2_numeric_poll()) controls_poll();
        pump(&jni, (jclass)1);
        const uint64_t pumped = sceKernelGetSystemTimeWide();
        extern void pvz2_gl_profile_begin(unsigned);
        pvz2_gl_profile_begin(frame);
        draw(&jni, (jclass)1);
        fjni_drain_glu_http();
        pvz2_keyboard_after_frame();
        const uint64_t drawn = sceKernelGetSystemTimeWide();
        gl_swap();
        const uint64_t presented = sceKernelGetSystemTimeWide();
        pump_us += pumped - begin;
        draw_us += drawn - pumped;
        present_us += presented - drawn;
        uint32_t frame_us = (uint32_t)(presented - begin);
        if (frame_us > peak_us) peak_us = frame_us;
        if (frame_us > 18000) ++slow_frames;
        ++frame;
        if ((frame % 300u) == 0u) {
            uint64_t now = sceKernelGetSystemTimeWide();
            uint32_t elapsed = (uint32_t)(now - sample_start);
            uint32_t fps_x100 = elapsed
                ? (uint32_t)((UINT64_C(300) * UINT64_C(100000000)) / elapsed)
                : 0;
            uint32_t live, created, freed;
            extern void fjni_ref_stats(uint32_t *, uint32_t *, uint32_t *);
            fjni_ref_stats(&live, &created, &freed);
            struct mallinfo heap = mallinfo();
            char audio[192];
            extern void opensl_format_stats(char *, size_t);
            opensl_format_stats(audio, sizeof(audio));
            char shaders[112];
            extern void pvz2_gl_shader_stats(char *, size_t);
            pvz2_gl_shader_stats(shaders, sizeof(shaders));
            char threads[176];
            extern void pvz2_threads_format_stats(char *, size_t);
            pvz2_threads_format_stats(threads, sizeof(threads));
            char pixels[144];
            pvz2_pixels_format_stats(pixels, sizeof(pixels));
            char touch_stats[160];
            controls_format_stats(touch_stats, sizeof(touch_stats));
            unsigned sampled_draw, upload, uploads;
            extern void pvz2_gl_profile_stats(unsigned *, unsigned *, unsigned *);
            pvz2_gl_profile_stats(&sampled_draw, &upload, &uploads);
            /* One bounded write per report, rather than opening/flushing the
             * memory card separately for every line on the rendering thread. */
            telemetry_log("60FPS", "frame=%u measured=%u.%02u fps\n"
                "[PERF] avg_us input=%u game=%u present=%u max=%u over18ms=%u/300 JNI_live=%u created=%u freed=%u heap_used=%u KiB\n"
                "[RENDER] sampled_draw_us=%u upload_us=%u uploads=%u\n"
                "[GPU] free KiB vram=%u ram=%u slow=%u\n[AUDIOQ] %s\n[SHADERS] %s\n[THREADS] %s\n[TOUCH] %s\n[PIXELS] %s",
                frame, fps_x100 / 100u, fps_x100 % 100u,
                (unsigned)(pump_us / 300), (unsigned)(draw_us / 300),
                (unsigned)(present_us / 300), peak_us, slow_frames,
                live, created, freed, (unsigned)(heap.uordblks / 1024),
                sampled_draw, upload, uploads,
                (unsigned)(vglMemFree(VGL_MEM_VRAM) / 1024),
                (unsigned)(vglMemFree(VGL_MEM_RAM) / 1024),
                (unsigned)(vglMemFree(VGL_MEM_SLOW) / 1024), audio, shaders, threads, touch_stats, pixels);
            pump_us = draw_us = present_us = 0;
            peak_us = slow_frames = 0;
            sample_start = now;
        }
    }
}

int main(void) {
    char setup_error[1024];
    /* No logger may create a conflicting file until old data is migrated. */
    if (!pvz2_prepare_userdata(setup_error, sizeof(setup_error))) pvz2_boot_screen(setup_error);
    telemetry_reset();
    telemetry_log("BOOT", "PvZ2 Vita 4.5.2 ROW 60-FPS direct loader");
    telemetry_log("BUILD", "452-v1.1-rc1 " __DATE__ " " __TIME__);
    int32_t epoch_probe = 6;
    bionic_tm local_epoch;
    if (bionic_localtime_r(&epoch_probe, &local_epoch))
        telemetry_log("TIME", "Android tm=%u; epoch+6 local=%04d-%02d-%02d %02d:%02d:%02d offset=%d",
                      (unsigned)sizeof(local_epoch), local_epoch.tm_year+1900,
                      local_epoch.tm_mon+1, local_epoch.tm_mday, local_epoch.tm_hour,
                      local_epoch.tm_min, local_epoch.tm_sec, local_epoch.tm_gmtoff);
    if (!pvz2_boot_check(setup_error, sizeof(setup_error))) fatal_error("%s", setup_error);
    telemetry_log("SETUP", "files/dependencies/writable save paths checked; OBB=%s", pvz2_obb_path());
    clocks_60fps();
    extern void pvz2_init_thread_affinity(void);
    pvz2_init_thread_affinity();
    extern int pvz2_cpu_core_count(void);
    pvz2_pixels_init((unsigned)pvz2_cpu_core_count() - 1u);
    log_memory_stage("startup");

    /* Catch a mixed hard/soft-float SDK before game data gets corrupted.
     * Volatile arguments prevent folding away the actual ABI calls. */
    volatile float angle = 0.5f;
    float sine = 0.0f, cosine = 0.0f;
    char *end = NULL;
    double parsed = strtod_sf("1.25", &end);
    sincosf_sf(angle, &sine, &cosine);
    if (parsed != 1.25 || !end || *end ||
        sine < 0.4794f || sine > 0.4795f ||
        cosine < 0.8775f || cosine > 0.8776f ||
        powf_sf(2.0f, 3.0f) != 8.0f) {
        telemetry_log("FATAL", "softfp math ABI self-check failed");
        fatal_error("This VPK failed its math compatibility check.\nInstall a fresh copy of the Vita release VPK.\n\nDetails: ux0:data/pvz2/userdata/loader.log");
    }
    telemetry_log("ABI", "softfp strtod/sincosf/powf self-check passed");
    /* Exercise the actual game import, including a binary NUL and a bounded
     * search. This catches the emulator's unimplemented sceClibMemchr bridge. */
    extern void *lookup_symbol_soloader_quiet(const char *);
    void *(*find_byte)(const void *, int, size_t) = lookup_symbol_soloader_quiet("memchr");
    const unsigned char probe[] = {'a', 0, '|', 'b'};
    if (!find_byte || find_byte(probe, '|', sizeof(probe)) != probe + 2 ||
        find_byte(probe, 0, sizeof(probe)) != probe + 1 ||
        find_byte(probe, '|', 2) || find_byte(probe, 'x', sizeof(probe))) {
        telemetry_log("FATAL", "memchr import self-check failed");
        fatal_error("This VPK failed its memory compatibility check.\nInstall a fresh copy of the Vita release VPK.\n\nDetails: ux0:data/pvz2/userdata/loader.log");
    }
    telemetry_log("ABI", "memchr import self-check passed");

    /* Match PvZ2Native boot_native_library: the fake Java runtime exists before
     * constructors, then init_array precedes JNI_OnLoad. */
    jni_init();
    controls_init();
    /* The image needs about 35 MiB at peak (file staging + mapped segments).
     * vitaGL claims free USER memory down to its 32 MiB reserve. Loading after
     * vglInit therefore fails on physical Vita with 0x80024302. Map/relocate
     * first and release staging BEFORE graphics measures its available pool.
     * Constructors still run after GL/JNI are ready, as in the tested order. */
    log_memory_stage("before library load");
    int load_result = load_exact_452();
    if (load_result != 0) {
        log_memory_stage("library load failed");
        telemetry_log("FATAL", "load/relocate/resolve failed");
        fatal_error("Cannot load the game library. Error: 0x%08x\n\nClose other apps and reboot your Vita.\nReinstall this VPK if the error continues.\n\nKeep ux0:data/pvz2/userdata/loader.log for support.", (unsigned)load_result);
    }
    log_memory_stage("library mapped; staging released");
    telemetry_log("BOOT", "initializing graphics");
    vita_rsb_start_preload();
    gl_preload();
    gl_init();
    log_memory_stage("graphics ready");
    run_init_array_logged();
    log_memory_stage("constructors complete");

    typedef jint (*JniOnLoad)(JavaVM *, void *);
    JniOnLoad onload = (JniOnLoad)so_symbol(&so_mod_pvz2, "JNI_OnLoad");
    if (!onload || onload(&jvm, NULL) == JNI_ERR) {
        telemetry_log("FATAL", "JNI_OnLoad failed");
        fatal_error("The game could not start its Android runtime.\nReinstall this VPK and the matching game files.\n\nKeep ux0:data/pvz2/userdata/loader.log for support.");
    }
    telemetry_log("BOOT", "init_array + JNI_OnLoad complete");

    run_ea_startup();
    if (run_game_initialize() != 0) {
        telemetry_log("FATAL", "GameAppInitialize returned false");
        fatal_error("The game could not initialize.\nCheck free space on ux0 and reboot your Vita.\n\nKeep ux0:data/pvz2/userdata/loader.log for support.\nYour saves have not been reset.");
    }
    run_lifecycle();
    run_60fps_loop();
    return 0;
}

void mcsm_register_virtual_controller(void) {}
void controls_handler_key(int32_t keycode, ControlsAction action) {
    /* PvZ2 maps only Android BACK/ENTER/MENU/DEL. Give Vita's face buttons the
     * useful equivalents while touch remains the primary control scheme. */
    int translated = keycode;
    if (keycode == AKEYCODE_BUTTON_A) translated = 66;       /* ENTER */
    else if (keycode == AKEYCODE_BUTTON_B) translated = 4;  /* BACK */
    else if (keycode == AKEYCODE_BUTTON_START) translated = 82; /* MENU */
    else if (keycode == AKEYCODE_BUTTON_X) translated = 67; /* DELETE */
    pvz2_input_push_key(translated, action == CONTROLS_ACTION_DOWN);
}
void controls_handler_touch(int32_t id, float x, float y, ControlsAction action) {
    int phase = PVZ2_INPUT_TOUCH_MOVE;
    if (action == CONTROLS_ACTION_DOWN) phase = PVZ2_INPUT_TOUCH_DOWN;
    else if (action == CONTROLS_ACTION_UP) phase = PVZ2_INPUT_TOUCH_UP;
    pvz2_input_push_touch(id, phase, (int)(x + 0.5f), (int)(y + 0.5f));
}
void controls_handler_analog(ControlsStickId which, float x, float y, ControlsAction action) {
    (void)which; (void)x; (void)y; (void)action;
}
