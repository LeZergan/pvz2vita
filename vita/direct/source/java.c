#include "utils/boot_check.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>

#include <falso_jni/FalsoJNI.h>
#include <falso_jni/FalsoJNI_Impl.h>
#include <falso_jni/FalsoJNI_Logger.h>
#include "reimpl/asset_manager.h"
#include "java_runtime.h"
#include "utils/glutil.h"
#include "utils/launch_state.h"
#include "utils/logger.h"
#include "utils/telemetry.h"
#include "reimpl/controls.h"
#include "reimpl/io.h"          /* remap_android_path — resolve ASSET:/leading-'/' paths */
#include "reimpl/rsb_index_vita.h"

enum mcsm_method_ids {
    MID_GET_EXTERNAL_STORAGE_DIRECTORY = 1000,
    MID_GET_PACKAGE_NAME = 1001,
    MID_GET_OBB_FILENAME = 1002,
    MID_GET_EXTERNAL_STORAGE_PATH = 1003,
    MID_GET_INTERNAL_STORAGE_PATH = 1004,
    MID_GET_ASSETS = 1005,
    MID_GET_CONTEXT = 1006,
    MID_GET_APPLICATION_CONTEXT = 1007,
    MID_GET_NATIVE_SURFACE = 1008,
    MID_FLIP_BUFFERS = 1009,
    MID_AUDIO_INIT = 1010,
    MID_AUDIO_WRITE_SHORT_BUFFER = 1011,
    MID_AUDIO_WRITE_BYTE_BUFFER = 1012,
    MID_AUDIO_QUIT = 1013,
    MID_POLL_INPUT_DEVICES = 1014,
    MID_INPUT_GET_INPUT_DEVICE_IDS = 1015,
    MID_GET_HARDWARE_MODEL = 1016,
    MID_SET_ACTIVITY_TITLE = 1017,
    MID_HAS_FEATURE = 1018,
    MID_SET_FRAMEBUFFER_SIZE = 1019,
    MID_GET_SAMPLE_RATE = 1020,
    MID_GET_OUTPUT_FRAMES_PER_BUFFER = 1021,
    MID_IS_USING_BLUETOOTH = 1022,
    MID_GET_HARDWARE_DISPLAY = 1023,
    MID_GET_HARDWARE_MANUFACTURER = 1024,
    MID_GET_XDPI = 1025,
    MID_GET_YDPI = 1026,
    MID_GET_WIDTH = 1027,
    MID_GET_HEIGHT = 1028,
    MID_GET_SCREEN_WIDTH = 1029,
    MID_GET_SCREEN_HEIGHT = 1030,
    MID_GET_SURFACE_WIDTH = 1031,
    MID_GET_SURFACE_HEIGHT = 1032,
    MID_GET_EXTERNAL_STORAGE_STATE = 1033,
    MID_GET_EXTERNAL_STORAGE_DIRS = 1034,
    MID_GET_FILES_DIR = 1035,
    MID_GET_ABSOLUTE_PATH = 1036,
    MID_CHECK_INIT = 1037,
    MID_SUPPORTS_LOW_LATENCY = 1038,
    MID_GET_ASSET_MANAGER = 1039,
    MID_FMOD_AUDIODEVICE_INIT = 1040,
    MID_GET_LOCALE = 1041,
    MID_GET_HARDWARE_OS = 1042,
    MID_GET_HARDWARE_BOARD = 1043,
    MID_AUDIO_DEVICE_INIT = 1044,
    MID_AUDIO_DEVICE_CLOSE = 1045,
    MID_AUDIO_DEVICE_WRITE = 1046,
    MID_IS_DATA_AVAILABLE = 1047,
    MID_IS_TV = 1048,
    MID_UPDATE_PURCHASES = 1049,
    MID_GET_OUTPUT_SAMPLE_RATE = 1050,
    MID_GET_OUTPUT_BLOCK_SIZE = 1051,
    MID_IS_PURCHASED = 1052,
    MID_GET_PURCHASED_SKUS = 1053,
    MID_REQUEST_PERMISSION = 1054,
    MID_IS_DOWNLOADED = 1055,
    MID_IS_DOWNLOADING = 1056,
    MID_GET_DOWNLOAD_PROGRESS = 1057,
    MID_CHECK_LICENSE = 1058,
    MID_IS_NETWORK_AVAILABLE = 1059,
    MID_GET_PURCHASE_PROVIDER = 1060,
    MID_IS_PRODUCT_PURCHASED = 1061,
    MID_PURCHASE = 1062,
    MID_ON_PURCHASE = 1063,
    MID_ON_UNLOCK_ACHIEVEMENT = 1064,
    MID_IS_SIGNED_IN = 1065,
    /* PvZ2 SexyAppFramework init methods */
    MID_UTIL_GET_UUID_STRING = 1100,
    MID_GENERATE_UUID = 1101,
    MID_GET_ANALYTICS_DEVICE_ID = 1102,
    MID_DIAG_GET_DEVICE_ID = 1103,
    /* SexyAppFramework resource/device methods (title/content loading) */
    MID_RES_GET_RESOURCE_FOLDER = 1104,
    MID_RES_GET_USER_DATA_FOLDER = 1105,
    MID_RES_GET_APP_SUPPORT_FOLDER = 1106,
    MID_RES_GET_CACHE_FOLDER = 1107,
    MID_RES_GET_EXT_STORAGE_DIR = 1108,
    MID_DEV_GET_CACHES_DIR = 1109,
    MID_DEV_GET_DEVICE_NAME = 1110,
    MID_RES_GET_FS_BLOCK_COUNT = 1111,
    MID_RES_GET_FS_BLOCK_SIZE = 1112,
    MID_RES_GET_FS_BLOCKS_FREE = 1113,
    MID_RES_GET_ASSET_FILE_SIZE = 1114,
    MID_DEV_IS_TABLET = 1115,
    MID_DEV_IS_KEYBOARD_SHOWING = 1116,
    MID_DEV_IS_SUPPORTED_ORIENTATION = 1117,
    MID_DEV_GET_CURRENT_ORIENTATION = 1118,
    MID_DEV_SHOW_KEYBOARD = 1119,
    MID_DEV_HIDE_KEYBOARD = 1120,
    MID_DEV_SHOW_NUMERIC_KEYBOARD = 1121,
    MID_DEV_EXIT_TO_HOME = 1122,
    MID_DEV_KILL_APPLICATION = 1123,
    MID_DEV_FORCE_ROTATE = 1124,
    MID_SYS_GET_MAIN_EXPANSION_PATH = 1125,
    MID_CFG_KEY_EXISTS = 1126,
    MID_CFG_READ_STRING = 1127,
    MID_CFG_READ_INT = 1128,
    MID_CFG_READ_BOOL = 1129,
    MID_CFG_WRITE_STRING = 1130,
    MID_CFG_WRITE_INT = 1131,
    MID_CFG_WRITE_BOOL = 1132,
    MID_CFG_ERASE_KEY = 1133,
    MID_STR_STORE_GET = 1134,
    MID_STR_STORE_SET = 1135,
    MID_PRIVATE_FILE_PATH = 1136,
    MID_CHECK_PRIVATE_DIR = 1137,
    MID_READ_SHARED_PROP = 1138,
    MID_GET_SESSION_ID = 1139,
    MID_GET_SYNERGY_ID = 1140,
    MID_GET_ANALYTICS_ENV = 1141,
    MID_GET_ANALYTICS_APP_NAME = 1142,
    MID_GET_APPLICATION_ID = 1143,
    MID_GET_REVENUE_ID = 1144,
    /* Version / locale getters (were unmapped -> empty-string stub -> invalid version) */
    MID_INFO_PRODUCT_VERSION = 1145,
    MID_GET_APPLICATION_VERSION = 1146,
    MID_INFO_PACKAGE_NAME = 1147,
    MID_INFO_ACTIVITY_NAME = 1148,
    MID_INFO_USER_LOCALE = 1149,
    MID_INFO_COUNTRY_CODE = 1150,
    MID_GET_LANGUAGE = 1151,
    MID_INFO_CURRENCY_CODE = 1152,
    MID_INFO_CURRENCY_SYMBOL = 1153,
    MID_INFO_PRODUCT_VERSION_INT = 1154,
    MID_RES_GET_ASSET_FILE_INFO = 1155,
    /* Device-capability queries — PvZ2 computes its resource/quality budget from
     * these; if they return 0 (the default stub) the budget calc degenerates and
     * the loader can stall. Give realistic values for a capable device. */
    MID_GET_RAM_AMOUNT = 1156,
    MID_GET_DEVICE_TIER = 1157,
    MID_GET_CPU_CORE_COUNT = 1158,
    MID_DIAG_MEM_TOTAL = 1159,
    MID_DIAG_MEM_AVAIL = 1160,
    MID_DIAG_MEM_USED = 1161,
    /* PvZ2Native AndroidSurfaceView hooks required by onSurfaceCreated. */
    MID_GFX_IS_GLES20 = 1162,
    MID_GFX_CAN_SET_SCALE = 1163,
    MID_GFX_SCREEN_PIXELS = 1164,
    MID_GFX_SCREEN_POINTS = 1165,
    MID_GFX_SYS_FBO = 1166,
    MID_UI_PROCESS_EVENTS = 1167,
};

/* Balanced default: 640x363 = 2/3 of native 960x544 (aspect-correct). Big
 * headroom over 3/4 (720x408, which dipped under 30 in heavy areas) so 30fps
 * holds as a hard floor, yet far sharper than 1/2 (480x272) and mipmaps keep it
 * clean. Tunable via fb_override.txt. */
#define MCSM_DEFAULT_RENDER_W 640
#define MCSM_DEFAULT_RENDER_H 363

static int g_fb_width = MCSM_DEFAULT_RENDER_W;
static int g_fb_height = MCSM_DEFAULT_RENDER_H;
static jobject g_asset_manager_obj = NULL;
static jobject g_context_obj = NULL;
static jobject g_native_surface_obj = NULL;
static int g_display_metrics_logged = 0;
#include "utils/pcm_blocks.h"
static PcmBlocks g_pcm_blocks;
static int g_audio_sample_rate = 48000;
static int g_audio_channels = 2;
static int g_audio_bytes_per_sample = 2;
static int g_audio_port = -1;
static int g_audio_port_frames = 1024;
static int g_audio_port_rate = 48000;
static int g_audio_port_channels = 2;
static int16_t *g_audio_scratch = NULL;
static int g_audio_scratch_samples = 0;
static int g_fb_override_loaded = 0;
static int g_fb_override_enabled = 1;
static int g_fb_override_width = MCSM_DEFAULT_RENDER_W;
static int g_fb_override_height = MCSM_DEFAULT_RENDER_H;
static unsigned int g_flipbuffers_count = 0;
static int g_request_permission_logged = 0;

/* Exact 4.5.2 AndroidGameApp.UI_ProcessEvents records, transcribed from the
 * upstream PvZ2Native input queue. This is the only input channel the game
 * consumes; native onTouch/onKey entry points do not exist in this build. */
#define PVZ2_INPUT_QUEUE_CAP 64

typedef struct PvZ2InputEvent {
    int kind; /* 0 touch, 1 key, 6 text */
    int phase_or_code;
    int x_or_down;
    int y;
    uint32_t touch_id;
    double time_ms;
    char text[257]; /* 64 UTF-16 units from IME, without byte truncation. */
} PvZ2InputEvent;

static PvZ2InputEvent g_input_queue[PVZ2_INPUT_QUEUE_CAP];
static unsigned g_input_head = 0;
static unsigned g_input_count = 0;
static uint32_t g_next_touch_id = 1;
/* Vita reports up to eight simultaneous contacts, with byte-sized source IDs.
 * A single global ID lets a second finger steal the first finger's release. */
static uint32_t g_touch_ids[256];

static double input_now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

static void input_enqueue(PvZ2InputEvent event) {
    if (g_input_count == PVZ2_INPUT_QUEUE_CAP) {
        g_input_head = (g_input_head + 1U) % PVZ2_INPUT_QUEUE_CAP;
        --g_input_count;
    }
    unsigned tail = (g_input_head + g_input_count) % PVZ2_INPUT_QUEUE_CAP;
    g_input_queue[tail] = event;
    ++g_input_count;
}

void pvz2_input_push_touch(int source_id, int phase, int x, int y) {
    if ((unsigned)source_id >= 256U) return;
    if (phase == PVZ2_INPUT_TOUCH_DOWN) {
        /* Recover a repeated DOWN without leaving its earlier capture alive. */
        if (g_touch_ids[source_id])
            pvz2_input_push_touch(source_id, PVZ2_INPUT_TOUCH_UP, x, y);
        uint32_t id = g_next_touch_id++;
        if (!id) id = g_next_touch_id++;
        g_touch_ids[source_id] = id;
    }
    if (!g_touch_ids[source_id]) return;
    PvZ2InputEvent event = {
        .kind = 0,
        .phase_or_code = phase,
        .x_or_down = x,
        .y = y,
        .touch_id = g_touch_ids[source_id],
        .time_ms = input_now_ms(),
    };
    input_enqueue(event);
    if (phase == PVZ2_INPUT_TOUCH_UP) g_touch_ids[source_id] = 0;
    static unsigned logs = 0;
    if (logs++ < 24U)
        l_info("[INPUT] queued touch phase=%d id=%u x=%d y=%d",
               phase, (unsigned)event.touch_id, x, y);
}

void pvz2_input_push_key(int android_keycode, int down) {
    PvZ2InputEvent event = {
        .kind = 1,
        .phase_or_code = android_keycode,
        .x_or_down = down,
        .time_ms = input_now_ms(),
    };
    input_enqueue(event);
}

void pvz2_input_push_text(const char *text) {
    if (!text) return;
    PvZ2InputEvent event = { .kind = 6 };
    snprintf(event.text, sizeof(event.text), "%s", text);
    input_enqueue(event);
}

static void input_put_u32(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}

static unsigned input_drain_to_buffer(uint8_t *buffer, size_t capacity) {
    if (!buffer || capacity < 16U) return 0;
    memset(buffer, 0, 16U);
    size_t cursor = 16U;
    unsigned delivered = 0;

    while (g_input_count) {
        PvZ2InputEvent *event = &g_input_queue[g_input_head];
        size_t text_length = event->kind == 6 ? strlen(event->text) : 0;
        size_t need = event->kind == 0 ? 48U : event->kind == 1 ? 32U :
                      12U + ((text_length + 3U) & ~3U);
        if (cursor + need > capacity) break;

        uint8_t *out = buffer + cursor;
        memset(out, 0, need);
        if (event->kind == 0) {
            input_put_u32(out + 0, 0);
            input_put_u32(out + 4, event->touch_id);
            input_put_u32(out + 8, (uint32_t)event->x_or_down);
            input_put_u32(out + 12, (uint32_t)event->y);
            input_put_u32(out + 16, (uint32_t)event->x_or_down);
            input_put_u32(out + 20, (uint32_t)event->y);
            memcpy(out + 28, &event->time_ms, sizeof(event->time_ms));
            input_put_u32(out + 36, (uint32_t)event->phase_or_code);
        } else if (event->kind == 1) {
            input_put_u32(out + 0, 1);
            input_put_u32(out + 4, (uint32_t)event->phase_or_code);
            input_put_u32(out + 8, 0); /* Unicode character, not down/up. */
            input_put_u32(out + 12, event->x_or_down ? 0 : 1); /* Android ACTION_DOWN/UP. */
            memcpy(out + 16, &event->time_ms, sizeof(event->time_ms));
        } else {
            /* Stock 4.5.2 text record: type, action (0=COMMIT), byte count, padded UTF-8. */
            input_put_u32(out, 6);
            input_put_u32(out + 8, (uint32_t)text_length);
            memcpy(out + 12, event->text, text_length);
            pvz2_keyboard_text_delivered();
        }

        cursor += need;
        ++delivered;
        g_input_head = (g_input_head + 1U) % PVZ2_INPUT_QUEUE_CAP;
        --g_input_count;
    }
    input_put_u32(buffer, delivered);
    return delivered;
}

static jboolean UIProcessEvents(jmethodID id, va_list args) {
    (void)id;
    jobject byte_buffer = va_arg(args, jobject);
    uint8_t *address = (uint8_t *)jni->GetDirectBufferAddress(&jni, byte_buffer);
    jlong capacity = jni->GetDirectBufferCapacity(&jni, byte_buffer);
    if (!address || capacity < 16)
        return JNI_FALSE;
    unsigned delivered = input_drain_to_buffer(address, (size_t)capacity);
    if (delivered) l_info("[INPUT] UI_ProcessEvents delivered=%u", delivered);
    return delivered ? JNI_TRUE : JNI_FALSE;
}

enum mcsm_field_ids {
    FID_WINDOW_SERVICE = 0,
    FID_SDK_INT = 1,
    FID_M_ASSET_MGR = 2,
};

static void sync_runtime_field_objects(void);

static void ensure_runtime_fields(void) {
    if (!g_asset_manager_obj) {
        g_asset_manager_obj = (jobject)AAssetManager_create();
    }
    if (!g_context_obj) {
        jobject local = jni->NewStringUTF(&jni, "context_main");
        g_context_obj = jni->NewGlobalRef(&jni, local);
        jni->DeleteLocalRef(&jni, local);
    }
    if (!g_native_surface_obj) {
        jobject local = jni->NewStringUTF(&jni, "native_surface");
        g_native_surface_obj = jni->NewGlobalRef(&jni, local);
        jni->DeleteLocalRef(&jni, local);
    }
    sync_runtime_field_objects();
}

static void sanitize_framebuffer_override(int *w, int *h, int *sanitized) {
    if (!w || !h || !sanitized || *w <= 0 || *h <= 0) return;

    if (*w <= 720) {
        int clean_h = ((*w * 9) + 8) / 16;
        if (clean_h & 1) {
            clean_h--;
        }
        int delta = *h - clean_h;
        if (delta < 0) {
            delta = -delta;
        }
        if (clean_h > 0 && delta > 0 && delta <= 4) {
            *h = clean_h;
            *sanitized = 1;
            return;
        }
    }

    if ((*h & 1) && *h > 180) {
        (*h)--;
        *sanitized = 1;
    }
}

static void ensure_framebuffer_override_loaded(void) {
    if (g_fb_override_loaded) return;
    g_fb_override_loaded = 1;
    char path[256];
    snprintf(path, sizeof(path), DATA_PATH "fb_override.txt");
    FILE *fp = fopen(path, "r");
    if (!fp) {
        l_info("Render-scale: %dx%d (default Vita performance mode)", g_fb_width, g_fb_height);
        return;
    }
    char line[64];
    int parsed = 0;
    if (fgets(line, sizeof(line), fp)) {
        int w = 0, h = 0;
        if (sscanf(line, "%dx%d", &w, &h) == 2 || sscanf(line, "%d %d", &w, &h) == 2) {
            if (w > 0 && h > 0) {
                const int requested_w = w;
                const int requested_h = h;
                int sanitized = 0;
                sanitize_framebuffer_override(&w, &h, &sanitized);
                g_fb_override_enabled = 1;
                g_fb_override_width = w;
                g_fb_override_height = h;
                g_fb_width = w;
                g_fb_height = h;
                parsed = 1;
                if (sanitized) {
                    l_info("Render-scale: %dx%d (from %s, requested=%dx%d sanitized)", w, h, path, requested_w, requested_h);
                } else {
                    l_info("Render-scale: %dx%d (from %s)", w, h, path);
                }
            }
        }
    }
    if (!parsed) {
        g_fb_override_enabled = 1;
        g_fb_override_width = MCSM_DEFAULT_RENDER_W;
        g_fb_override_height = MCSM_DEFAULT_RENDER_H;
        g_fb_width = MCSM_DEFAULT_RENDER_W;
        g_fb_height = MCSM_DEFAULT_RENDER_H;
        l_info("Render-scale: %dx%d (invalid %s, using default Vita performance mode)",
               g_fb_width, g_fb_height, path);
    }
    fclose(fp);
}

static int clamp_audio_rate(int rate) {
    switch (rate) {
        case 8000: case 11025: case 12000: case 16000:
        case 22050: case 24000: case 32000: case 44100:
        case 48000: return rate;
        default: return 48000;
    }
}

static int clamp_audio_channels(int channels) { return (channels == 1) ? 1 : 2; }

static int clamp_audio_frames(int frames) {
    if (frames <= 0) frames = 1024;
    if (frames < SCE_AUDIO_MIN_LEN) frames = SCE_AUDIO_MIN_LEN;
    if (frames > 4096) frames = 4096;
    frames = (frames + 63) & ~63;
    if (frames > SCE_AUDIO_MAX_LEN) frames = SCE_AUDIO_MAX_LEN;
    return frames;
}

#define AUDIO_GAIN_Q8_DEFAULT 320 /* 1.25x. Vita hardware volume is already 0 dB. */

static int audio_gain_q8(void) {
    static int s_gain_q8 = -1;
    if (s_gain_q8 < 0) {
        int percent = 125;
        char path[256];
        snprintf(path, sizeof(path), DATA_PATH "audio_gain.txt");
        FILE *fp = fopen(path, "r");
        if (!fp) {
            fp = fopen(DATA_PATH "audio_gain.txt", "r");
        }
        if (fp) {
            char buf[32];
            if (fgets(buf, sizeof(buf), fp)) {
                int requested = atoi(buf);
                if (requested >= 50 && requested <= 200) {
                    percent = requested;
                }
            }
            fclose(fp);
        }
        s_gain_q8 = (percent * 256 + 50) / 100;
        l_info("AUDIO gain=%d%%", percent);
    }
    return s_gain_q8;
}

static int16_t audio_apply_gain_i16(int16_t sample, int gain_q8) {
    int value = ((int)sample * gain_q8) / 256;
    if (value > 32767) {
        value = 32767;
    } else if (value < -32768) {
        value = -32768;
    }
    return (int16_t)value;
}

static void audio_write_sleep_us(int byte_count) {
    if (byte_count <= 0) return;
    int sample_rate = g_audio_sample_rate > 0 ? g_audio_sample_rate : 48000;
    int channels = g_audio_channels > 0 ? g_audio_channels : 2;
    int bytes_per_sample = g_audio_bytes_per_sample > 0 ? g_audio_bytes_per_sample : 2;
    int bytes_per_frame = channels * bytes_per_sample;
    if (bytes_per_frame <= 0) bytes_per_frame = 4;
    int64_t usec = ((int64_t)byte_count * 1000000LL) / ((int64_t)sample_rate * (int64_t)bytes_per_frame);
    if (usec < 1000) usec = 1000;
    else if (usec > 100000) usec = 100000;
    sceKernelDelayThread((unsigned int)usec);
}

void audio_reset_stream(void) { g_pcm_blocks.pending = 0; }

void audio_close_port(void) {
    audio_reset_stream();
    if (g_audio_port >= 0) {
        int rc = sceAudioOutReleasePort(g_audio_port);
        l_info("AUDIO close port=%d rc=0x%08X", g_audio_port, (unsigned)rc);
        g_audio_port = -1;
    }
}

static int audio_ensure_scratch(int samples) {
    if (samples <= g_audio_scratch_samples) return 1;
    int16_t *new_buf = realloc(g_audio_scratch, (size_t)samples * sizeof(int16_t));
    if (!new_buf) { l_error("AUDIO scratch alloc failed (%d samples).", samples); return 0; }
    g_audio_scratch = new_buf; g_audio_scratch_samples = samples;
    return 1;
}

int audio_open_port(int sample_rate, int channels, int desired_frames) {
    const int rate = clamp_audio_rate(sample_rate);
    const int out_channels = clamp_audio_channels(channels);
    const int frames = clamp_audio_frames(desired_frames);
    if (g_audio_port >= 0 && g_audio_port_rate == rate &&
        g_audio_port_channels == out_channels && g_audio_port_frames == frames)
        return g_audio_port_frames;
    audio_close_port();
    const SceAudioOutMode mode = (out_channels == 1) ? SCE_AUDIO_OUT_MODE_MONO : SCE_AUDIO_OUT_MODE_STEREO;
    telemetry_log("AUDIO", "sceAudioOutOpenPort BGM frames=%d rate=%d ch=%d",
                  frames, rate, out_channels);
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, frames, rate, mode);
    telemetry_log("AUDIO", "sceAudioOutOpenPort BGM -> 0x%x", (unsigned)port);
    int actual_rate = rate;
    /* Do not play a 32 kHz stream at 48 kHz if BGM fails. MAIN only supports
     * 48 kHz; report failure for other rates instead of changing speed/pitch. */
    if (port < 0 && rate == 48000)
        port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, frames, rate, mode);
    if (port < 0) {
        l_error("AUDIO open failed rate=%d frames=%d ch=%d rc=0x%08X", rate, frames, out_channels, (unsigned)port);
        return port;
    }
    g_audio_port = port; g_audio_port_frames = frames; g_audio_port_rate = actual_rate; g_audio_port_channels = out_channels;
    g_audio_sample_rate = actual_rate;
    g_audio_channels = out_channels;
    g_audio_bytes_per_sample = 2;
    int volume[2] = { SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB };
    telemetry_log("AUDIO", "sceAudioOutSetVolume port=%d", g_audio_port);
    sceAudioOutSetVolume(g_audio_port, (SceAudioOutChannelFlag)(SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH), volume);
    telemetry_log("AUDIO", "audio port ready port=%d rate=%d frames=%d", g_audio_port,
                  g_audio_port_rate, g_audio_port_frames);
    l_info("AUDIO open port=%d rate=%d frames=%d ch=%d", g_audio_port, g_audio_port_rate, g_audio_port_frames, g_audio_port_channels);
    return g_audio_port_frames;
}

static int audio_submit_block(const int16_t *samples, void *ctx) {
    (void)ctx;
    return sceAudioOutOutput(g_audio_port, samples);
}

void audio_output_i16_frames(const int16_t *samples, int frames, int channels) {
    if (!samples || frames <= 0) return;
    channels = clamp_audio_channels(channels);
    if (audio_open_port(g_audio_sample_rate, channels, g_audio_port_frames) < 0) {
        audio_write_sleep_us(frames * channels * 2);
        return;
    }
    int rc = pcm_blocks_write(&g_pcm_blocks, samples, (unsigned)frames,
        (unsigned)channels, (unsigned)g_audio_port_frames, audio_gain_q8(),
        audio_submit_block, NULL);
    if (rc < 0) {
        static unsigned failures;
        if (failures++ < 4) l_error("AUDIO output failed rc=0x%08X", (unsigned)rc);
        audio_write_sleep_us(frames * channels * 2);
    }
}

static void audio_output_byte_buffer(const jbyte *bytes, int byte_count) {
    const int channels = clamp_audio_channels(g_audio_channels);
    const int bps = (g_audio_bytes_per_sample == 1 || g_audio_bytes_per_sample == 4) ? g_audio_bytes_per_sample : 2;
    const int bytes_per_frame = channels * bps;
    if (!bytes || byte_count < bytes_per_frame) return;
    audio_open_port(g_audio_sample_rate, channels, g_audio_port_frames);
    int frames = byte_count / bytes_per_frame;
    int offset_frames = 0;
    while (offset_frames < frames) {
        int chunk_frames = frames - offset_frames;
        if (chunk_frames > g_audio_port_frames) chunk_frames = g_audio_port_frames;
        const int chunk_samples = chunk_frames * channels;
        const int scratch_samples = g_audio_port_frames * channels;
        if (!audio_ensure_scratch(scratch_samples)) { audio_write_sleep_us(chunk_frames * bytes_per_frame); return; }
        const jbyte *src = bytes + (offset_frames * bytes_per_frame);
        for (int i = 0; i < chunk_samples; i++) {
            if (bps == 1) g_audio_scratch[i] = (int16_t)(((int)((uint8_t)src[i]) - 128) << 8);
            else if (bps == 4) { int32_t value = ((int32_t)(uint8_t)src[i*4]) | ((int32_t)(uint8_t)src[i*4+1]<<8) | ((int32_t)(uint8_t)src[i*4+2]<<16) | ((int32_t)(uint8_t)src[i*4+3]<<24); g_audio_scratch[i] = (int16_t)(value>>16); }
            else g_audio_scratch[i] = (int16_t)(((uint16_t)(uint8_t)src[i*2]) | ((uint16_t)(uint8_t)src[i*2+1]<<8));
        }
        audio_output_i16_frames(g_audio_scratch, chunk_frames, channels);
        offset_frames += chunk_frames;
    }
}

static jobject ret_string(const char *value) {
    ensure_runtime_fields();
    if (!value) return NULL;
    return jni->NewStringUTF(&jni, value);
}

static jobject GetExternalStorageDirectory(jmethodID id, va_list args) { (void)id; (void)args; return ret_string("/sdcard"); }
static jobject GetPackageName(jmethodID id, va_list args) { (void)id; (void)args; return ret_string("com.ea.game.pvz2_row"); }
static jobject GetObbFileName(jmethodID id, va_list args) { (void)id; const int is_main = va_arg(args, int); if (is_main) return ret_string(pvz2_obb_path()); return ret_string(DATA_PATH "patch.147.com.ea.game.pvz2_row.obb"); }
/* FrameworkInfo_SysGetMainExpansionFilePath()Ljava/lang/String; — the resource
 * loader calls this (statically) to locate the main OBB. When stubbed it returned
 * null, so the engine opened "" (open(,0):-1) and computed garbage OBB offsets
 * (seek to 0x811C2588). Return the real OBB path so it can read resources. */
static jobject SysGetMainExpansionFilePath(jmethodID id, va_list args) {
    (void)id; (void)args;
    /* PvZ2Native's critical contract: a leading slash makes
     * ResStreamsManager::LoadRSB use this path verbatim.  Without it the game
     * routes the whole 656 MB OBB through Resources_GetAssetFileInfo. */
    char path[256];
    snprintf(path, sizeof(path), "/%s", pvz2_obb_path());
    return ret_string(path);
}
static jobject GetExternalStoragePath(jmethodID id, va_list args) { (void)id; (void)args; return ret_string(DATA_PATH); }
static jobject GetInternalStoragePath(jmethodID id, va_list args) { (void)id; (void)args; return ret_string("/data/data/com.ea.game.pvz2_row/files"); }
/* NON-ZERO UUID/device IDs. The Glu Tags/config system treats an all-zero (or
 * empty) analytics/device ID as "not set" and queues every config lookup
 * (SDK_CONFIG_CONSENT, ...) forever, hanging boot on the consent step. Return
 * stable non-zero identifiers so the analytics ID counts as "set". */
static jobject UtilGetUUIDString(jmethodID id, va_list args) { (void)id; (void)args; return ret_string("a1b2c3d4-e5f6-4788-9abc-def012345678"); }
static jobject GenerateUUID(jmethodID id, va_list args) { (void)id; (void)args; return ret_string("a1b2c3d4-e5f6-4788-9abc-def012345679"); }
static jobject GetAnalyticsDeviceIdentifier(jmethodID id, va_list args) { (void)id; (void)args; return ret_string("a1b2c3d4-e5f6-4788-9abc-def01234567a"); }
static jobject DiagGetDeviceID(jmethodID id, va_list args) { (void)id; (void)args; return ret_string("PSVITA00a1b2c3d4e5f6"); }

/* Vita's fake Java layer exposes the writable ux0 data directory directly. */
static jobject ResourcesGetResourceFolder(jmethodID id, va_list a)       { (void)id; (void)a; return ret_string(DATA_PATH); }
static jobject ResourcesGetUserDataFolder(jmethodID id, va_list a)        { (void)id; (void)a; return ret_string(DATA_PATH); }
static jobject ResourcesGetAppSupportDataFolder(jmethodID id, va_list a)  { (void)id; (void)a; return ret_string(DATA_PATH); }
static jobject ResourcesGetCacheDataFolder(jmethodID id, va_list a)       { (void)id; (void)a; return ret_string(DATA_PATH "cache/"); }
static jobject ResourcesGetExtStorageDir(jmethodID id, va_list a)         { (void)id; (void)a; return ret_string(DATA_PATH); }
static jobject DeviceGetCachesDir(jmethodID id, va_list a)                { (void)id; (void)a; return ret_string(DATA_PATH "cache/"); }
static jobject DeviceGetDeviceName(jmethodID id, va_list a)               { (void)id; (void)a; return ret_string("PS Vita"); }

/* Helper: pull a C path out of a jstring arg. */
static void jstr_to_path(jstring js, char *buf, size_t n) {
    buf[0] = 0;
    if (!js) return;
    const char *s = jni->GetStringUTFChars(&jni, js, NULL);
    if (s) { snprintf(buf, n, "%s", s); jni->ReleaseStringUTFChars(&jni, js, s); }
}

/* ------------------------------------------------------------------ *
 *  Config store (SexyAppFramework Java Config_* + Glu string store)
 * ------------------------------------------------------------------ *
 * The game persists small config values via the Java side (Config_ConfigWrite*
 * then polls Config_ConfigKeyExists until true). Stubbing these false made the
 * loading screen poll ConfigKeyExists FOREVER (the boolpoll stall). Implement a
 * real key/value store: in-memory table + line-based persistence at
 * ux0:data/pvz2/config.kv ("key\tvalue\n"). Values stored as strings; int/bool
 * are converted. Keys/values are small (config flags, ids). */
#define CFG_MAX 256
#define CFG_KV_PATH DATA_PATH "config.kv"
static char g_cfg_keys[CFG_MAX][96];
static char g_cfg_vals[CFG_MAX][256];
static int  g_cfg_count = 0;
static int  g_cfg_loaded = 0;

static void cfg_load(void) {
    if (g_cfg_loaded) return;
    g_cfg_loaded = 1;
    FILE *f = fopen(CFG_KV_PATH, "rb");
    if (!f) return;
    char line[368];
    while (fgets(line, sizeof(line), f) && g_cfg_count < CFG_MAX) {
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = 0;
        char *val = tab + 1;
        char *nl = strchr(val, '\n');
        if (nl) *nl = 0;
        snprintf(g_cfg_keys[g_cfg_count], sizeof(g_cfg_keys[0]), "%s", line);
        snprintf(g_cfg_vals[g_cfg_count], sizeof(g_cfg_vals[0]), "%s", val);
        g_cfg_count++;
    }
    fclose(f);
    l_info("[CFG] loaded %d keys from %s", g_cfg_count, CFG_KV_PATH);
}

static void cfg_save(void) {
    FILE *f = fopen(CFG_KV_PATH, "wb");
    if (!f) { l_warn("[CFG] save failed"); return; }
    for (int i = 0; i < g_cfg_count; i++)
        fprintf(f, "%s\t%s\n", g_cfg_keys[i], g_cfg_vals[i]);
    fclose(f);
}

static int cfg_find(const char *key) {
    for (int i = 0; i < g_cfg_count; i++)
        if (strcmp(g_cfg_keys[i], key) == 0) return i;
    return -1;
}

static void cfg_set(const char *key, const char *val) {
    cfg_load();
    int i = cfg_find(key);
    if (i < 0) {
        if (g_cfg_count >= CFG_MAX) { l_warn("[CFG] table full, dropping %s", key); return; }
        i = g_cfg_count++;
        snprintf(g_cfg_keys[i], sizeof(g_cfg_keys[0]), "%s", key);
    }
    snprintf(g_cfg_vals[i], sizeof(g_cfg_vals[0]), "%s", val);
    cfg_save();
}

/* Offline gate flags the game polls KeyExists() on forever, waiting for an async
 * online op (that never completes offline) to create them. It only checks their
 * EXISTENCE (never reads the value), so pre-seeding unblocks the loading screen.
 * wctgc = the flag that hung boot at ~frame 3300 (3440 polls). Add more here if
 * a new KeyExists() spin appears. */
static void cfg_seed_gate(const char *key) {
    if (cfg_find(key) < 0 && g_cfg_count < CFG_MAX) {
        snprintf(g_cfg_keys[g_cfg_count], sizeof(g_cfg_keys[0]), "%s", key);
        snprintf(g_cfg_vals[g_cfg_count], sizeof(g_cfg_vals[0]), "1");
        g_cfg_count++;
    }
}

/* Kept for main.c's extern; no longer used for stack-walking (that diagnostic
 * done — it overran the stack on a shallow call and crashed). */
uintptr_t g_pvz2_text_base_for_diag = 0;
extern void *g_fjni_bool_caller;   /* set by FalsoJNI CallBooleanMethod* = game call site */
static jboolean ConfigKeyExists(jmethodID id, va_list a) {
    (void)id; cfg_load();
    (void)cfg_seed_gate;   /* wctgc seed removed; see wctgc handling below */
    char key[96]; jstr_to_path(va_arg(a, jstring), key, sizeof(key));
    /* wctgc = the per-frame loading-screen gate. (1) Force it ABSENT — bypass the
     * persisted store (which still holds wctgc=1 from earlier seeded runs) to test
     * whether a pending-op flag being SET was holding boot. (2) Log the native
     * caller (game code that polls it) so Ghidra can decompile the exact gate. */
    /* wctgc: confirmed RED HERRING (game loops whether present or absent). Report it
     * absent (bypass the persisted store). The stack-scan diagnostic that used to be
     * here OVERRAN the stack on a shallow call and data-aborted (ConfigKeyExists+0x10e)
     * — the native chain was already captured, so it's removed. */
    if (strcmp(key, "wctgc") == 0) return JNI_FALSE;
    int found = cfg_find(key) >= 0;
    l_debug("[CFG] KeyExists(%s) = %d", key, found);
    return found ? JNI_TRUE : JNI_FALSE;
}
static jobject ConfigReadString(jmethodID id, va_list a) {
    (void)id; cfg_load();
    char key[96]; jstr_to_path(va_arg(a, jstring), key, sizeof(key));
    int i = cfg_find(key);
    if (i < 0) {
        /* Match Android SharedPreferences.getString(key, null): an absent key
         * is JNI null, which is distinct from a present empty string. */
        l_debug("[CFG] ReadString(%s) = <null>", key);
        return NULL;
    }
    l_debug("[CFG] ReadString(%s) = '%s'", key, g_cfg_vals[i]);
    return ret_string(g_cfg_vals[i]);
}
static jint ConfigReadInteger(jmethodID id, va_list a) {
    (void)id; cfg_load();
    char key[96]; jstr_to_path(va_arg(a, jstring), key, sizeof(key));
    int i = cfg_find(key);
    return (i >= 0) ? (jint)strtol(g_cfg_vals[i], NULL, 10) : 0;
}
static jboolean ConfigReadBoolean(jmethodID id, va_list a) {
    (void)id; cfg_load();
    char key[96]; jstr_to_path(va_arg(a, jstring), key, sizeof(key));
    int i = cfg_find(key);
    if (i < 0) return JNI_FALSE;
    return (strcmp(g_cfg_vals[i], "1") == 0 || strcmp(g_cfg_vals[i], "true") == 0) ? JNI_TRUE : JNI_FALSE;
}
static jboolean ConfigWriteString(jmethodID id, va_list a) {
    (void)id;
    char key[96], val[256];
    jstr_to_path(va_arg(a, jstring), key, sizeof(key));
    jstr_to_path(va_arg(a, jstring), val, sizeof(val));
    l_info("[CFG] WriteString(%s, '%s')", key, val);
    cfg_set(key, val);
    return JNI_TRUE;
}
static jboolean ConfigWriteInteger(jmethodID id, va_list a) {
    (void)id;
    char key[96], val[32];
    jstr_to_path(va_arg(a, jstring), key, sizeof(key));
    snprintf(val, sizeof(val), "%d", va_arg(a, jint));
    cfg_set(key, val);
    return JNI_TRUE;
}
static jboolean ConfigWriteBoolean(jmethodID id, va_list a) {
    (void)id;
    char key[96]; jstr_to_path(va_arg(a, jstring), key, sizeof(key));
    int v = va_arg(a, jint);  /* jboolean promotes to int in varargs */
    cfg_set(key, v ? "1" : "0");
    return JNI_TRUE;
}
static void ConfigEraseKey(jmethodID id, va_list a) {
    (void)id; cfg_load();
    char key[96]; jstr_to_path(va_arg(a, jstring), key, sizeof(key));
    int i = cfg_find(key);
    if (i >= 0) {
        g_cfg_count--;
        if (i != g_cfg_count) {
            memcpy(g_cfg_keys[i], g_cfg_keys[g_cfg_count], sizeof(g_cfg_keys[0]));
            memcpy(g_cfg_vals[i], g_cfg_vals[g_cfg_count], sizeof(g_cfg_vals[0]));
        }
        cfg_save();
    }
}
/* Glu string store — same backing store, "ss:" prefix to avoid collisions. */
static jobject StringStoreGet(jmethodID id, va_list a) {
    (void)id; cfg_load();
    char key[96] = "ss:";
    jstr_to_path(va_arg(a, jstring), key + 3, sizeof(key) - 3);
    int i = cfg_find(key);
    return ret_string(i >= 0 ? g_cfg_vals[i] : "");
}
static void StringStoreSet(jmethodID id, va_list a) {
    (void)id;
    char key[96] = "ss:", val[256];
    jstr_to_path(va_arg(a, jstring), key + 3, sizeof(key) - 3);
    jstr_to_path(va_arg(a, jstring), val, sizeof(val));
    cfg_set(key, val);
}
/* Private data dir helpers (Glu). Point at our data dir; "exists" after we
 * mkdir it (create arg honored). */
static jobject PrivateFilePath(jmethodID id, va_list a) { (void)id; (void)a; return ret_string(DATA_PATH "private"); }
static jboolean CheckPrivateDirectoryExists(jmethodID id, va_list a) {
    (void)id;
    char sub[96]; jstr_to_path(va_arg(a, jstring), sub, sizeof(sub));
    int create = va_arg(a, jint);
    char path[256];
    snprintf(path, sizeof(path), DATA_PATH "private/%s", sub);
    struct stat st;
    if (stat(path, &st) == 0) return JNI_TRUE;
    if (create) {
        mkdir(DATA_PATH "private", 0777);
        mkdir(path, 0777);
        return (stat(path, &st) == 0) ? JNI_TRUE : JNI_FALSE;
    }
    return JNI_FALSE;
}
static jobject ReadSharedProperty(jmethodID id, va_list a) {
    (void)id; cfg_load();
    char key[96] = "sp:";
    jstr_to_path(va_arg(a, jstring), key + 3, sizeof(key) - 3);
    int i = cfg_find(key);
    return ret_string(i >= 0 ? g_cfg_vals[i] : "");
}

/* Analytics/identity IDs. The Glu Tags/config system QUEUES every getTag until
 * the "analytics ID" (the session id `s=`) is set — offline that comes from
 * getSessionIdentifier(), which was stubbed to "" so the whole online init hung
 * ("analytics ID is not set - add to pending queue"). Return a stable non-empty
 * session id so setUserID gets s=NNN, the ID is "set", pending getTags flush,
 * and the init completes. Others report plausible offline values. */
static jobject GetSessionIdentifier(jmethodID id, va_list a)      { (void)id; (void)a; return ret_string("5E551011-0000-4788-9ABC-DEF012345678"); }
static jobject GetSynergyId(jmethodID id, va_list a)              { (void)id; (void)a; return ret_string(""); }
static jobject GetAnalyticsEnvironment(jmethodID id, va_list a)   { (void)id; (void)a; return ret_string("production"); }
static jobject GetAnalyticsApplicationName(jmethodID id, va_list a){ (void)id; (void)a; return ret_string("PvZ2"); }
static jobject GetApplicationID(jmethodID id, va_list a)          { (void)id; (void)a; return ret_string("com.ea.pvz2"); }
static jobject GetRevenueIdentifier(jmethodID id, va_list a)      { (void)id; (void)a; return ret_string(""); }
/* Match PvZ2Native's 4.5.2 AndroidGameApp hooks.  This old build does not use
 * the modern 13.x content suffix: the upstream default product-version string
 * is empty and Info_SysGetProductVersion returns 1. */
static jobject SysGetProductVersionString(jmethodID id, va_list a){ (void)id; (void)a; return ret_string(""); }
static jobject GetApplicationVersion(jmethodID id, va_list a)     { (void)id; (void)a; return ret_string("4.5.2"); }
static jobject SysGetPackageName(jmethodID id, va_list a)         { (void)id; (void)a; return ret_string("com.ea.game.pvz2_row"); }
static jobject SysGetActivityName(jmethodID id, va_list a)        { (void)id; (void)a; return ret_string("com.popcap.PvZ2.PvZ2GameActivity"); }
static jobject SysGetUserLocale(jmethodID id, va_list a)          { (void)id; (void)a; return ret_string("en_US"); }
static jobject SysGetCountryCode(jmethodID id, va_list a)         { (void)id; (void)a; return ret_string("US"); }
static jobject GetLanguage(jmethodID id, va_list a)               { (void)id; (void)a; return ret_string("en"); }
static jobject SysGetUserCurrencyCode(jmethodID id, va_list a)    { (void)id; (void)a; return ret_string("USD"); }
static jobject SysGetUserCurrencySymbol(jmethodID id, va_list a)  { (void)id; (void)a; return ret_string("$"); }
static jint SysGetProductVersionInt(jmethodID id, va_list a)      { (void)id; (void)a; return 1; }

/* Filesystem stats — report a big, healthy volume so the game's free-space
 * checks pass (path arg ignored). ~8GB free of a 16GB volume, 4K blocks. */
static jlong ResourcesGetFsBlockCount(jmethodID id, va_list a) { (void)id; (void)a; return 4194304; }
static jlong ResourcesGetFsBlockSize(jmethodID id, va_list a)  { (void)id; (void)a; return 4096; }
static jlong ResourcesGetFsBlocksFree(jmethodID id, va_list a) { (void)id; (void)a; return 2097152; }

/* Match PvZ2Native's RSB-backed Android AssetManager contract exactly. */
static jlong ResourcesGetAssetFileSize(jmethodID id, va_list a) {
    (void)id;
    jstring js = va_arg(a, jstring);
    char name[512]; jstr_to_path(js, name, sizeof(name));
    uint64_t offset = 0;
    uint32_t size = 0;
    int compressed = 0;
    if (vita_rsb_find(name, &offset, &size, &compressed) && !compressed)
        return (jlong)size;
    return (jlong)-1;
}

/* Android AssetFileDescriptor equivalent: return a readable container and its
 * uncompressed resource range. Compressed regular RSG blocks are cached lazily;
 * textures use the native RSB loader instead. */
static jobject ResourcesGetAssetFileInfo(jmethodID id, va_list a) {
    (void)id;
    jstring js = va_arg(a, jstring);
    jlongArray out = va_arg(a, jlongArray);
    char name[512]; jstr_to_path(js, name, sizeof(name));
    jlong info[2] = { 0, 0 };
    uint64_t offset = 0;
    uint32_t size = 0;
    const char *container = vita_rsb_locate(name, &offset, &size);
    if (!container || !out) {
        if (out) jni->SetLongArrayRegion(&jni, out, 0, 2, info);
        static unsigned misses = 0;
        if (misses++ < 48)
            l_info("[asset] '%s' -> %s", name,
                   !container ? "NOT READABLE IN RSB" : "no out array");
        return NULL;
    }

    info[0] = (jlong)offset;
    info[1] = (jlong)size;
    jni->SetLongArrayRegion(&jni, out, 0, 2, info);
    static unsigned hits = 0;
    if (hits++ < 48 || strstr(name, ".pam") || strstr(name, ".PAM"))
        l_info("[asset] '%s' -> %s off=%u size=%u", name, container,
               (unsigned)offset, (unsigned)size);
    return ret_string(container);
}

/* Device queries. */
static jboolean DeviceIsTablet(jmethodID id, va_list a)              { (void)id; (void)a; return JNI_FALSE; }
static jboolean DeviceIsKeyboardShowing(jmethodID id, va_list a)     { (void)id; (void)a; return pvz2_keyboard_is_showing() ? JNI_TRUE : JNI_FALSE; }
static void DeviceShowNumericKeyboard(jmethodID id, va_list a)       { (void)id; (void)a; pvz2_numeric_request(1); }
static void DeviceShowKeyboard(jmethodID id, va_list a)              { (void)id; (void)a; pvz2_text_request(); }
static void DeviceHideKeyboard(jmethodID id, va_list a)              { (void)id; (void)a; pvz2_numeric_request(0); }
static jboolean DeviceIsSupportedOrientation(jmethodID id, va_list a){ (void)id; (void)a; return JNI_TRUE; }
/* Android configuration constant used by the 4.5.2 host path for the
 * landscape-right surface.  The engine branches on this during surface init. */
static jint     DeviceGetCurrentOrientation(jmethodID id, va_list a) { (void)id; (void)a; return 4; }
static void     DeviceVoidNoop(jmethodID id, va_list a)              { (void)id; (void)a; }
static jboolean GraphicsIsOpenGLES20(jmethodID id, va_list a)        { (void)id; (void)a; return JNI_TRUE; }
static jboolean GraphicsCanSetScale(jmethodID id, va_list a)         { (void)id; (void)a; return JNI_TRUE; }
static jint GraphicsGetSysFBO(jmethodID id, va_list a)               { (void)id; (void)a; return 0; }
static void GraphicsGetScreenSize(jmethodID id, va_list a) {
    (void)id;
    jintArray out = va_arg(a, jintArray);
    const jint size[2] = { 960, 544 };
    if (out) jni->SetIntArrayRegion(&jni, out, 0, 2, size);
}
static jobject GetContext(jmethodID id, va_list args) { (void)id; (void)args; ensure_runtime_fields(); return jni->NewLocalRef(&jni, g_context_obj); }
static jobject GetApplicationContext(jmethodID id, va_list args) { (void)id; (void)args; ensure_runtime_fields(); return jni->NewLocalRef(&jni, g_context_obj); }
static jobject GetAssets(jmethodID id, va_list args) { (void)id; (void)args; ensure_runtime_fields(); return g_asset_manager_obj; }
static jobject GetNativeSurface(jmethodID id, va_list args) { (void)id; (void)args; ensure_runtime_fields(); return jni->NewLocalRef(&jni, g_native_surface_obj); }
static jobject GetExternalStorageState(jmethodID id, va_list args) { (void)id; (void)args; return ret_string("mounted"); }
static jobject GetExternalStorageDirs(jmethodID id, va_list args) { (void)id; (void)args; jobjectArray arr = jni->NewObjectArray(&jni, 1, NULL, NULL); jni->SetObjectArrayElement(&jni, arr, 0, ret_string(DATA_PATH)); return arr; }
static jobject GetFilesDir(jmethodID id, va_list args) { (void)id; (void)args; return ret_string(DATA_PATH); }
static jobject GetAbsolutePath(jmethodID id, va_list args) { (void)id; (void)args; return ret_string(DATA_PATH); }

static void FlipBuffers(jmethodID id, va_list args) {
    (void)id; (void)args; gl_swap(); g_flipbuffers_count++;
    if (g_flipbuffers_count <= 8 || (g_flipbuffers_count & 0x7fU) == 0U) l_info("FlipBuffers count=%u", g_flipbuffers_count);
}

static jint AudioInit(jmethodID id, va_list args) {
    (void)id;
    const int sample_rate = va_arg(args, int), is_16bit = va_arg(args, int), is_stereo = va_arg(args, int), desired_frames = va_arg(args, int);
    g_audio_sample_rate = sample_rate > 0 ? sample_rate : 48000;
    g_audio_channels = is_stereo ? 2 : 1; g_audio_bytes_per_sample = is_16bit ? 2 : 1;
    int frames = audio_open_port(g_audio_sample_rate, g_audio_channels, desired_frames);
    l_info("AUDIO AudioTrack.init rate=%d bits=%d stereo=%d desired=%d -> frames=%d", g_audio_sample_rate, is_16bit ? 16 : 8, is_stereo, desired_frames, frames);
    return frames;
}

static void AudioWriteShortBuffer(jmethodID id, va_list args) {
    (void)id; jobject buffer = va_arg(args, jobject);
    int sample_count = buffer ? jni->GetArrayLength(&jni, buffer) : 0;
    { static unsigned int s_aws = 0; if (s_aws++ < 4U) l_info("AUDIO writeShort #%u samples=%d (FMOD driving audio)", s_aws, sample_count); }
    if (sample_count <= 0) sample_count = 1024;
    jshort *samples = jni->GetShortArrayElements(&jni, (jshortArray)buffer, NULL);
    if (samples) { audio_output_i16_frames(samples, sample_count / clamp_audio_channels(g_audio_channels), clamp_audio_channels(g_audio_channels)); jni->ReleaseShortArrayElements(&jni, (jshortArray)buffer, samples, 0); }
    else audio_write_sleep_us(sample_count * 2);
}

static void AudioWriteByteBuffer(jmethodID id, va_list args) {
    (void)id; jobject buffer = va_arg(args, jobject);
    int byte_count = buffer ? jni->GetArrayLength(&jni, buffer) : 0;
    { static unsigned int s_awb = 0; if (s_awb++ < 4U) l_info("AUDIO writeByte #%u bytes=%d (FMOD driving audio)", s_awb, byte_count); }
    if (byte_count <= 0) byte_count = 4096;
    jbyte *bytes = jni->GetByteArrayElements(&jni, (jbyteArray)buffer, NULL);
    if (bytes) { audio_output_byte_buffer(bytes, byte_count); jni->ReleaseByteArrayElements(&jni, (jbyteArray)buffer, bytes, 0); }
    else audio_write_sleep_us(byte_count);
}

static void AudioQuit(jmethodID id, va_list args) { (void)id; (void)args; audio_close_port(); }
extern void mcsm_register_virtual_controller(void);
static void PollInputDevices(jmethodID id, va_list args) { (void)id; (void)args; launch_state_mark_poll(); mcsm_register_virtual_controller(); }

static void UpdatePurchases(jmethodID id, va_list args) {
    static unsigned log_count = 0; (void)id; (void)args;
    if (log_count < 16U) { l_info("DLC updatePurchases -> no-op"); log_count++; }
}

static jboolean IsNetworkAvailable(jmethodID id, va_list args) {
    static unsigned log_count = 0; (void)id; (void)args;
    if (log_count < 16U) { l_info("DLC isNetworkAvailable -> false (offline local-data mode)"); log_count++; }
    return JNI_FALSE;
}

static jobject GetPurchaseProvider(jmethodID id, va_list args) {
    static unsigned log_count = 0; (void)id; (void)args;
    if (log_count < 8U) { l_info("DLC getPurchaseProvider -> Amazon"); log_count++; }
    return ret_string("Amazon");
}

static void RequestPermission(jmethodID id, va_list args) {
    (void)id; int permission_code = va_arg(args, int);
    if (g_request_permission_logged < 16) { l_info("DLC requestPermission(%d) -> granted", permission_code); g_request_permission_logged++; }
}

static jboolean IsPurchased(jmethodID id, va_list args) {
    static unsigned log_count = 0; (void)id; jobject sku = va_arg(args, jobject);
    const char *sku_name = sku ? jni->GetStringUTFChars(&jni, (jstring)sku, NULL) : NULL;
    if (log_count < 16U) { l_info("DLC purchase check(\"%s\") -> true", sku_name ? sku_name : "(null)"); log_count++; }
    if (sku_name) jni->ReleaseStringUTFChars(&jni, (jstring)sku, (char *)sku_name);
    return JNI_TRUE;
}

static void Purchase(jmethodID id, va_list args) {
    static unsigned log_count = 0; (void)id; (void)args;
    if (log_count < 8U) { l_info("DLC purchase -> no-op, already owned locally"); log_count++; }
}

static void OnPurchase(jmethodID id, va_list args) {
    static unsigned log_count = 0; (void)id; (void)args;
    if (log_count < 8U) { l_info("DLC onPurchase -> no-op"); log_count++; }
}

static void OnUnlockAchievement(jmethodID id, va_list args) {
    (void)id;
    (void)args;
}

static jobject GetPurchasedSkus(jmethodID id, va_list args) {
    static unsigned log_count = 0; (void)id; (void)args;
    const char *skus[] = { "MCSM_Episode_101","MCSM_Episode_102","MCSM_Episode_103","MCSM_Episode_104","MCSM_Episode_105","MCSM_Episode_106","MCSM_Episode_107","MCSM_Episode_108" };
    const int num_skus = sizeof(skus)/sizeof(skus[0]);
    jobjectArray arr = jni->NewObjectArray(&jni, num_skus, NULL, NULL);
    if (!arr) return NULL;
    for (int i = 0; i < num_skus; i++) { jobject str = jni->NewStringUTF(&jni, skus[i]); jni->SetObjectArrayElement(&jni, arr, i, str); jni->DeleteLocalRef(&jni, str); }
    if (log_count < 4U) { l_info("DLC getPurchasedSkus -> [%d skus]", num_skus); log_count++; }
    return arr;
}

// Download manager stubs: the game's DownloadManager probes Google Play
// service endpoints for OBB download progress and license verification.
// On Vita all data is pre-installed at ux0:data/pvz2/ — immediately report
// "downloaded, not downloading, 100% progress, license valid".
static jboolean IsDownloaded(jmethodID id, va_list args) {
    static unsigned log_count = 0; (void)id; (void)args;
    if (log_count < 8U) { l_info("DLC isDownloaded -> true"); log_count++; }
    return JNI_TRUE;
}

static jboolean IsDownloading(jmethodID id, va_list args) {
    static unsigned log_count = 0; (void)id; (void)args;
    if (log_count < 4U) { l_info("DLC isDownloading -> false"); log_count++; }
    return JNI_FALSE;
}

static jint GetDownloadProgress(jmethodID id, va_list args) {
    static unsigned log_count = 0; (void)id; (void)args;
    if (log_count < 4U) { l_info("DLC getDownloadProgress -> 100%%"); log_count++; }
    return 100;
}

static jboolean CheckLicense(jmethodID id, va_list args) {
    static unsigned log_count = 0; (void)id; jobject sku = va_arg(args, jobject);
    const char *sku_name = sku ? jni->GetStringUTFChars(&jni, (jstring)sku, NULL) : NULL;
    if (log_count < 16U) { l_info("DLC checkLicense(\"%s\") -> valid", sku_name ? sku_name : "(null)"); log_count++; }
    if (sku_name) jni->ReleaseStringUTFChars(&jni, (jstring)sku, (char *)sku_name);
    return JNI_TRUE;
}

static jobject InputGetInputDeviceIds(jmethodID id, va_list args) {
    (void)id; const int source_mask = va_arg(args, int);
    jintArray devices = jni->NewIntArray(&jni, 1); if (!devices) return NULL;
    const jint vita_device_id = 0;
    jni->SetIntArrayRegion(&jni, devices, 0, 1, &vita_device_id);
    l_info("inputGetInputDeviceIds(source=0x%08X) -> [0]", (unsigned)source_mask);
    return devices;
}

/* Device-capability queries. PvZ2 reads these ONCE at init to pick a memory/quality
 * tier; a 0 from the default stub can degenerate its budget math and stall the
 * loader. Report a capable-but-modest device (~2GB) so the tier check passes; the
 * loader's own texture downsampling still caps actual VRAM/RAM use. */
static jdouble GetRamAmount(jmethodID id, va_list args) {
    (void)id; (void)args;
    static int n = 0; if (n++ < 2) l_info("getRamAmount -> 768MB");
    return 805306368.0; /* v1321: 2GB->768MB — the game sizes its resource cache from this; 2GB overfilled the 256MB Vita heap (real-Vita frame-7 OOM). */
}
static jint GetDeviceTier(jmethodID id, va_list args) { (void)id; (void)args; return 2; }
static jint GetCpuCoreCount(jmethodID id, va_list args) { (void)id; (void)args; extern int pvz2_cpu_core_count(void); return pvz2_cpu_core_count(); }
static jlong DiagGetMemoryTotal(jmethodID id, va_list args)     { (void)id; (void)args; return 805306368LL; }  /* v1321: 768 MiB */
static jlong DiagGetMemoryAvailable(jmethodID id, va_list args) { (void)id; (void)args; return 268435456LL; }  /* v1321: 256 MiB (< heap) */
static jlong DiagGetMemoryUsed(jmethodID id, va_list args)      { (void)id; (void)args; return 268435456LL; }  /* 256 MiB */

static jobject GetHardwareModel(jmethodID id, va_list args) { (void)id; (void)args; return ret_string("PlayStation Vita"); }
static jboolean SetActivityTitle(jmethodID id, va_list args) { (void)id; (void)args; return JNI_TRUE; }

static jboolean HasFeature(jmethodID id, va_list args) {
    (void)id; jobject feature = va_arg(args, jobject);
    const char *feature_name = feature ? jni->GetStringUTFChars(&jni, (jstring)feature, NULL) : NULL;
    const int is_low_latency = feature_name && (strstr(feature_name, "audio.low_latency") || strstr(feature_name, "FEATURE_AUDIO_LOW_LATENCY"));
    static unsigned log_count = 0;
    if (log_count < 4U) { l_info("Feature query \"%s\" -> false%s", feature_name ? feature_name : "(null)", is_low_latency ? " (low-latency disabled for FMOD output)" : ""); log_count++; }
    if (feature_name) jni->ReleaseStringUTFChars(&jni, (jstring)feature, (char *)feature_name);
    return JNI_FALSE;
}

static void SetFramebufferSize(jmethodID id, va_list args) {
    (void)id; ensure_framebuffer_override_loaded(); int w = va_arg(args, int), h = va_arg(args, int);
    if (g_fb_override_enabled) { g_fb_width = g_fb_override_width; g_fb_height = g_fb_override_height; l_info("SetFramebufferSize(%d,%d) -> render-scale %dx%d", w, h, g_fb_width, g_fb_height); return; }
    g_fb_width = MCSM_DEFAULT_RENDER_W; g_fb_height = MCSM_DEFAULT_RENDER_H;
    l_info("SetFramebufferSize(%d,%d) -> native %dx%d", w, h, g_fb_width, g_fb_height);
}

static jint GetSampleRate(jmethodID id, va_list args) { (void)id; (void)args; return 48000; }
static jint GetOutputFramesPerBuffer(jmethodID id, va_list args) { (void)id; (void)args; return 1024; }
static jint GetOutputSampleRate(jmethodID id, va_list args) { (void)id; (void)args; return 48000; }
static jint GetOutputBlockSize(jmethodID id, va_list args) { (void)id; (void)args; return 1024; }
static jboolean IsUsingBluetooth(jmethodID id, va_list args) { (void)id; (void)args; return JNI_FALSE; }
static jboolean CheckInit(jmethodID id, va_list args) { (void)id; (void)args; return JNI_TRUE; }

static jboolean IsDataAvailable(jmethodID id, va_list args) {
    static unsigned log_count = 0; (void)id; (void)args;
    if (log_count < 16U) { l_info("DLC isDataAvailable -> true"); log_count++; }
    return JNI_TRUE;
}

static jboolean IsTV(jmethodID id, va_list args) { (void)id; (void)args; return JNI_FALSE; }
static jboolean SupportsLowLatency(jmethodID id, va_list args) { (void)id; (void)args; return JNI_FALSE; }
static jboolean IsSignedIn(jmethodID id, va_list args) {
    static unsigned log_count = 0; (void)id; (void)args;
    if (log_count < 8U) { l_info("JNI isSignedIn -> true (#%u)", log_count + 1U); log_count++; }
    return JNI_TRUE;
}

static jobject GetAssetManager(jmethodID id, va_list args) { (void)id; (void)args; ensure_runtime_fields(); return g_asset_manager_obj; }
static jobject FmodAudioDeviceInit(jmethodID id, va_list args) { (void)id; (void)args; return ret_string("fmod_audio_device"); }
static jobject GetLocale(jmethodID id, va_list args) { (void)id; (void)args; return ret_string("en_US"); }
static jobject GetHardwareOS(jmethodID id, va_list args) { (void)id; (void)args; return ret_string("Android 4.4.4"); }
static jobject GetHardwareBoard(jmethodID id, va_list args) { (void)id; (void)args; return ret_string("vita"); }

static jboolean AudioDeviceInit(jmethodID id, va_list args) {
    (void)id; int sample_rate = va_arg(args, int), channels_or_config = va_arg(args, int), bits_or_encoding = va_arg(args, int), desired_frames = va_arg(args, int);
    if (sample_rate > 0) g_audio_sample_rate = sample_rate;
    if (channels_or_config == 1 || channels_or_config == 2) g_audio_channels = channels_or_config;
    else if (channels_or_config == 4) g_audio_channels = 1; else if (channels_or_config == 12) g_audio_channels = 2;
    if (bits_or_encoding == 8 || bits_or_encoding == 3) g_audio_bytes_per_sample = 1;
    else if (bits_or_encoding == 16 || bits_or_encoding == 2) g_audio_bytes_per_sample = 2;
    else if (bits_or_encoding == 32 || bits_or_encoding == 4) g_audio_bytes_per_sample = 4;
    audio_open_port(g_audio_sample_rate, g_audio_channels, desired_frames);
    l_info("AUDIO AudioDevice.init args=%d,%d,%d,%d -> rate=%d ch=%d bps=%d frames=%d", sample_rate, channels_or_config, bits_or_encoding, desired_frames, g_audio_sample_rate, g_audio_channels, g_audio_bytes_per_sample, g_audio_port_frames);
    return JNI_TRUE;
}

static void AudioDeviceClose(jmethodID id, va_list args) { (void)id; (void)args; audio_close_port(); }
static void AudioDeviceWrite(jmethodID id, va_list args) {
    (void)id; jobject buffer = va_arg(args, jobject); int size = va_arg(args, int);
    int byte_count = buffer ? jni->GetArrayLength(&jni, buffer) : 0;
    if (size > 0 && (byte_count == 0 || size < byte_count)) byte_count = size;
    if (size <= 0) size = 4096;
    jbyte *bytes = buffer ? jni->GetByteArrayElements(&jni, (jbyteArray)buffer, NULL) : NULL;
    if (bytes && byte_count > 0) { audio_output_byte_buffer(bytes, byte_count); jni->ReleaseByteArrayElements(&jni, (jbyteArray)buffer, bytes, 0); }
    else audio_write_sleep_us(size);
}

static jobject GetHardwareDisplay(jmethodID id, va_list args) { (void)id; (void)args; ensure_framebuffer_override_loaded(); char buf[32]; snprintf(buf, sizeof(buf), "%dx%d", g_fb_width, g_fb_height); return ret_string(buf); }
static jobject GetHardwareManufacturer(jmethodID id, va_list args) { (void)id; (void)args; return ret_string("Sony"); }
/* DPI scaled with the render resolution: the engine sizes UI in density-independent
 * pixels (dp = px*160/dpi), so at a lower render res we must report a proportionally
 * lower DPI, otherwise UI elements keep their native pixel size and overflow the
 * smaller screen (the "overscan"). At native (g_fb_width==960) this is exactly 220. */
static jfloat mcsm_scaled_dpi(void) { ensure_framebuffer_override_loaded(); return 220.0f * (float)g_fb_width / 960.0f; }
static jfloat GetXDPI(jmethodID id, va_list args) { (void)id; (void)args; float d = mcsm_scaled_dpi(); if (!g_display_metrics_logged) { l_info("Display metrics: fb=%dx%d dpi=%.1f (native 220 scaled by render res)", g_fb_width, g_fb_height, d); g_display_metrics_logged = 1; } return d; }
static jfloat GetYDPI(jmethodID id, va_list args) { (void)id; (void)args; return mcsm_scaled_dpi(); }
static jint GetWidth(jmethodID id, va_list args) { (void)id; (void)args; ensure_framebuffer_override_loaded();
    { static unsigned s_gw = 0; if ((s_gw++ & 0x3fU) == 0) l_info("DRCAP GetWidth call #%u -> %d (is it per-frame? = clean dynres possible)", s_gw, g_fb_width); }
    return g_fb_width; }
static jint GetHeight(jmethodID id, va_list args) { (void)id; (void)args; ensure_framebuffer_override_loaded(); return g_fb_height; }

int mcsm_get_framebuffer_width(void) { ensure_framebuffer_override_loaded(); return (g_fb_width > 0) ? g_fb_width : MCSM_DEFAULT_RENDER_W; }
int mcsm_get_framebuffer_height(void) { ensure_framebuffer_override_loaded(); return (g_fb_height > 0) ? g_fb_height : MCSM_DEFAULT_RENDER_H; }
/* Render-scale = the low-res the GAME's frame is rendered at into the FBO before
 * upscaling to native. From fb_override.txt, independent of the game's logical res. */
int mcsm_get_render_scale_width(void) { ensure_framebuffer_override_loaded(); return g_fb_override_enabled ? g_fb_override_width : MCSM_DEFAULT_RENDER_W; }
int mcsm_get_render_scale_height(void) { ensure_framebuffer_override_loaded(); return g_fb_override_enabled ? g_fb_override_height : MCSM_DEFAULT_RENDER_H; }

/*
 * JNI Methods
 */
NameToMethodID nameToMethodId[] = {
    { MID_GET_EXTERNAL_STORAGE_DIRECTORY, "getExternalStorageDirectory", METHOD_TYPE_OBJECT },
    { MID_GET_PACKAGE_NAME, "getPackageName", METHOD_TYPE_OBJECT },
    { MID_GET_OBB_FILENAME, "getObbFileName", METHOD_TYPE_OBJECT },
    { MID_SYS_GET_MAIN_EXPANSION_PATH, "FrameworkInfo_SysGetMainExpansionFilePath", METHOD_TYPE_OBJECT },
    { MID_CFG_KEY_EXISTS, "Config_ConfigKeyExists", METHOD_TYPE_BOOLEAN },
    { MID_CFG_KEY_EXISTS, "configValueExists", METHOD_TYPE_BOOLEAN },
    { MID_CFG_READ_STRING, "Config_ConfigReadString", METHOD_TYPE_OBJECT },
    { MID_CFG_READ_INT, "Config_ConfigReadInteger", METHOD_TYPE_INT },
    { MID_CFG_READ_BOOL, "Config_ConfigReadBoolean", METHOD_TYPE_BOOLEAN },
    { MID_CFG_WRITE_STRING, "Config_ConfigWriteString", METHOD_TYPE_BOOLEAN },
    { MID_CFG_WRITE_INT, "Config_ConfigWriteInteger", METHOD_TYPE_BOOLEAN },
    { MID_CFG_WRITE_BOOL, "Config_ConfigWriteBoolean", METHOD_TYPE_BOOLEAN },
    { MID_CFG_ERASE_KEY, "Config_ConfigEraseKey", METHOD_TYPE_VOID },
    { MID_STR_STORE_GET, "getFromStringStore", METHOD_TYPE_OBJECT },
    { MID_STR_STORE_SET, "setToStringStore", METHOD_TYPE_VOID },
    { MID_PRIVATE_FILE_PATH, "privateFilePath", METHOD_TYPE_OBJECT },
    { MID_CHECK_PRIVATE_DIR, "checkPrivateDirectoryExists", METHOD_TYPE_BOOLEAN },
    { MID_READ_SHARED_PROP, "readSharedProperty", METHOD_TYPE_OBJECT },
    { MID_GET_SESSION_ID, "getSessionIdentifier", METHOD_TYPE_OBJECT },
    { MID_GET_SYNERGY_ID, "getSynergyId", METHOD_TYPE_OBJECT },
    { MID_GET_ANALYTICS_ENV, "getAnalyticsEnvironment", METHOD_TYPE_OBJECT },
    { MID_GET_ANALYTICS_APP_NAME, "getAnalyticsApplicationName", METHOD_TYPE_OBJECT },
    { MID_GET_APPLICATION_ID, "getApplicationID", METHOD_TYPE_OBJECT },
    { MID_GET_REVENUE_ID, "getRevenueIdentifier", METHOD_TYPE_OBJECT },
    { MID_INFO_PRODUCT_VERSION, "Info_SysGetProductVersionString", METHOD_TYPE_OBJECT },
    { MID_GET_APPLICATION_VERSION, "getApplicationVersion", METHOD_TYPE_OBJECT },
    { MID_INFO_PACKAGE_NAME, "Info_SysGetPackageName", METHOD_TYPE_OBJECT },
    { MID_INFO_ACTIVITY_NAME, "Info_SysGetActivityName", METHOD_TYPE_OBJECT },
    { MID_INFO_USER_LOCALE, "Info_SysGetUserLocale", METHOD_TYPE_OBJECT },
    { MID_INFO_COUNTRY_CODE, "Info_SysGetCountryCodeString", METHOD_TYPE_OBJECT },
    { MID_GET_LANGUAGE, "getLanguage", METHOD_TYPE_OBJECT },
    { MID_INFO_CURRENCY_CODE, "Info_SysGetUserCurrencyCode", METHOD_TYPE_OBJECT },
    { MID_INFO_CURRENCY_SYMBOL, "Info_SysGetUserCurrencySymbol", METHOD_TYPE_OBJECT },
    { MID_INFO_PRODUCT_VERSION_INT, "Info_SysGetProductVersion", METHOD_TYPE_INT },
    { MID_GET_EXTERNAL_STORAGE_PATH, "getExternalStoragePath", METHOD_TYPE_OBJECT },
    { MID_GET_INTERNAL_STORAGE_PATH, "getInternalStoragePath", METHOD_TYPE_OBJECT },
    { MID_UTIL_GET_UUID_STRING, "Util_GetUUIDString", METHOD_TYPE_OBJECT },
    { MID_GENERATE_UUID, "generateUUID", METHOD_TYPE_OBJECT },
    { MID_GET_ANALYTICS_DEVICE_ID, "getAnalyticsDeviceIdentifier", METHOD_TYPE_OBJECT },
    { MID_DIAG_GET_DEVICE_ID, "Diag_GetDeviceID", METHOD_TYPE_OBJECT },
    { MID_GET_HARDWARE_OS, "Diag_GetOSVersion", METHOD_TYPE_OBJECT },
    { MID_GET_HARDWARE_MODEL, "Diag_GetHardwareModel", METHOD_TYPE_OBJECT },
    { MID_RES_GET_RESOURCE_FOLDER, "Resources_GetResourceFolder", METHOD_TYPE_OBJECT },
    { MID_RES_GET_USER_DATA_FOLDER, "Resources_GetUserDataFolder", METHOD_TYPE_OBJECT },
    { MID_RES_GET_APP_SUPPORT_FOLDER, "Resources_GetAppSupportDataFolder", METHOD_TYPE_OBJECT },
    { MID_RES_GET_CACHE_FOLDER, "Resources_GetCacheDataFolder", METHOD_TYPE_OBJECT },
    { MID_RES_GET_EXT_STORAGE_DIR, "Resources_GetExternalStorageDirectory", METHOD_TYPE_OBJECT },
    { MID_DEV_GET_CACHES_DIR, "Device_GetCachesDir", METHOD_TYPE_OBJECT },
    { MID_DEV_GET_DEVICE_NAME, "Device_GetDeviceName", METHOD_TYPE_OBJECT },
    { MID_RES_GET_FS_BLOCK_COUNT, "Resources_GetFileSystemBlockCount", METHOD_TYPE_LONG },
    { MID_RES_GET_FS_BLOCK_SIZE, "Resources_GetFileSystemBlockSize", METHOD_TYPE_LONG },
    { MID_RES_GET_FS_BLOCKS_FREE, "Resources_GetFileSystemBlocksFree", METHOD_TYPE_LONG },
    { MID_RES_GET_ASSET_FILE_SIZE, "Resources_GetAssetFileSize", METHOD_TYPE_LONG },
    { MID_RES_GET_ASSET_FILE_INFO, "Resources_GetAssetFileInfo", METHOD_TYPE_OBJECT },
    { MID_GET_RAM_AMOUNT, "getRamAmount", METHOD_TYPE_DOUBLE },
    { MID_GET_DEVICE_TIER, "getDeviceTier", METHOD_TYPE_INT },
    { MID_GET_CPU_CORE_COUNT, "getCpuCoreCount", METHOD_TYPE_INT },
    { MID_DIAG_MEM_TOTAL, "Diag_GetMemoryTotal", METHOD_TYPE_LONG },
    { MID_DIAG_MEM_AVAIL, "Diag_GetMemoryAvailable", METHOD_TYPE_LONG },
    { MID_DIAG_MEM_USED, "Diag_GetMemoryUsed", METHOD_TYPE_LONG },
    { MID_GFX_IS_GLES20, "Graphics_IsOpenGLES20", METHOD_TYPE_BOOLEAN },
    { MID_GFX_CAN_SET_SCALE, "Graphics_CanSetGLViewScaleFactor", METHOD_TYPE_BOOLEAN },
    { MID_GFX_SCREEN_PIXELS, "Graphics_GetScreenSizeInPixels", METHOD_TYPE_VOID },
    { MID_GFX_SCREEN_POINTS, "Graphics_GetScreenSizeInPoints", METHOD_TYPE_VOID },
    { MID_GFX_SYS_FBO, "Graphics_GetGLViewSysFBO", METHOD_TYPE_INT },
    { MID_UI_PROCESS_EVENTS, "UI_ProcessEvents", METHOD_TYPE_BOOLEAN },
    { MID_DEV_IS_TABLET, "Device_IsTablet", METHOD_TYPE_BOOLEAN },
    { MID_DEV_IS_KEYBOARD_SHOWING, "Device_IsKeyboardShowing", METHOD_TYPE_BOOLEAN },
    { MID_DEV_IS_SUPPORTED_ORIENTATION, "Device_IsSupportedUIOrientation", METHOD_TYPE_BOOLEAN },
    { MID_DEV_GET_CURRENT_ORIENTATION, "Device_GetCurrentUIOrientation", METHOD_TYPE_INT },
    { MID_DEV_SHOW_KEYBOARD, "Device_ShowKeyboard", METHOD_TYPE_VOID },
    { MID_DEV_HIDE_KEYBOARD, "Device_HideKeyboard", METHOD_TYPE_VOID },
    { MID_DEV_SHOW_NUMERIC_KEYBOARD, "Device_ShowNumericKeyboard", METHOD_TYPE_VOID },
    { MID_DEV_EXIT_TO_HOME, "Device_ExitToHome", METHOD_TYPE_VOID },
    { MID_DEV_KILL_APPLICATION, "Device_KillApplication", METHOD_TYPE_VOID },
    { MID_DEV_FORCE_ROTATE, "Device_ForceRotateToOrientation", METHOD_TYPE_VOID },
    { MID_GET_ASSETS, "getAssets", METHOD_TYPE_OBJECT },
    { MID_GET_CONTEXT, "getContext", METHOD_TYPE_OBJECT },
    { MID_GET_APPLICATION_CONTEXT, "getApplicationContext", METHOD_TYPE_OBJECT },
    { MID_GET_NATIVE_SURFACE, "getNativeSurface", METHOD_TYPE_OBJECT },
    { MID_FLIP_BUFFERS, "flipBuffers", METHOD_TYPE_VOID },
    { MID_AUDIO_INIT, "audioInit", METHOD_TYPE_INT },
    { MID_AUDIO_WRITE_SHORT_BUFFER, "audioWriteShortBuffer", METHOD_TYPE_VOID },
    { MID_AUDIO_WRITE_BYTE_BUFFER, "audioWriteByteBuffer", METHOD_TYPE_VOID },
    { MID_AUDIO_QUIT, "audioQuit", METHOD_TYPE_VOID },
    { MID_POLL_INPUT_DEVICES, "pollInputDevices", METHOD_TYPE_VOID },
    { MID_INPUT_GET_INPUT_DEVICE_IDS, "inputGetInputDeviceIds", METHOD_TYPE_OBJECT },
    { MID_GET_HARDWARE_MODEL, "getHardwareModel", METHOD_TYPE_OBJECT },
    { MID_SET_ACTIVITY_TITLE, "setActivityTitle", METHOD_TYPE_BOOLEAN },
    { MID_HAS_FEATURE, "hasFeature", METHOD_TYPE_BOOLEAN },
    { MID_SET_FRAMEBUFFER_SIZE, "setFramebufferSize", METHOD_TYPE_VOID },
    { MID_GET_SAMPLE_RATE, "getSampleRate", METHOD_TYPE_INT },
    { MID_GET_OUTPUT_FRAMES_PER_BUFFER, "getOutputFramesPerBuffer", METHOD_TYPE_INT },
    { MID_IS_USING_BLUETOOTH, "isUsingBluetooth", METHOD_TYPE_BOOLEAN },
    { MID_GET_HARDWARE_DISPLAY, "getHardwareDisplay", METHOD_TYPE_OBJECT },
    { MID_GET_HARDWARE_MANUFACTURER, "getHardwareManufacturer", METHOD_TYPE_OBJECT },
    { MID_GET_XDPI, "getXDPI", METHOD_TYPE_FLOAT },
    { MID_GET_YDPI, "getYDPI", METHOD_TYPE_FLOAT },
    { MID_GET_WIDTH, "getWidth", METHOD_TYPE_INT },
    { MID_GET_HEIGHT, "getHeight", METHOD_TYPE_INT },
    { MID_GET_SCREEN_WIDTH, "getScreenWidth", METHOD_TYPE_INT },
    { MID_GET_SCREEN_HEIGHT, "getScreenHeight", METHOD_TYPE_INT },
    { MID_GET_SURFACE_WIDTH, "getSurfaceWidth", METHOD_TYPE_INT },
    { MID_GET_SURFACE_HEIGHT, "getSurfaceHeight", METHOD_TYPE_INT },
    { MID_GET_EXTERNAL_STORAGE_STATE, "getExternalStorageState", METHOD_TYPE_OBJECT },
    { MID_GET_EXTERNAL_STORAGE_DIRS, "getExternalStorageDirs", METHOD_TYPE_OBJECT },
    { MID_GET_FILES_DIR, "getFilesDir", METHOD_TYPE_OBJECT },
    { MID_GET_ABSOLUTE_PATH, "getAbsolutePath", METHOD_TYPE_OBJECT },
    { MID_CHECK_INIT, "checkInit", METHOD_TYPE_BOOLEAN },
    { MID_SUPPORTS_LOW_LATENCY, "supportsLowLatency", METHOD_TYPE_BOOLEAN },
    { MID_GET_ASSET_MANAGER, "getAssetManager", METHOD_TYPE_OBJECT },
    { MID_FMOD_AUDIODEVICE_INIT, "org/fmod/AudioDevice/<init>", METHOD_TYPE_OBJECT },
    { MID_GET_LOCALE, "getLocale", METHOD_TYPE_OBJECT },
    { MID_GET_HARDWARE_OS, "getHardwareOS", METHOD_TYPE_OBJECT },
    { MID_GET_HARDWARE_BOARD, "getHardwareBoard", METHOD_TYPE_OBJECT },
    { MID_AUDIO_DEVICE_INIT, "init", METHOD_TYPE_BOOLEAN },
    { MID_AUDIO_DEVICE_CLOSE, "close", METHOD_TYPE_VOID },
    { MID_AUDIO_DEVICE_WRITE, "write", METHOD_TYPE_VOID },
    { MID_IS_DATA_AVAILABLE, "isDataAvailable", METHOD_TYPE_BOOLEAN },
    { MID_IS_TV, "isTV", METHOD_TYPE_BOOLEAN },
    { MID_UPDATE_PURCHASES, "updatePurchases", METHOD_TYPE_VOID },
    { MID_GET_OUTPUT_SAMPLE_RATE, "getOutputSampleRate", METHOD_TYPE_INT },
    { MID_GET_OUTPUT_BLOCK_SIZE, "getOutputBlockSize", METHOD_TYPE_INT },
    { MID_IS_PURCHASED, "isPurchased", METHOD_TYPE_BOOLEAN },
    { MID_IS_PRODUCT_PURCHASED, "isProductPurchased", METHOD_TYPE_BOOLEAN },
    { MID_GET_PURCHASED_SKUS, "getPurchasedSkus", METHOD_TYPE_OBJECT },
    { MID_GET_PURCHASE_PROVIDER, "getPurchaseProvider", METHOD_TYPE_OBJECT },
    { MID_REQUEST_PERMISSION, "requestPermission", METHOD_TYPE_VOID },
    { MID_PURCHASE, "purchase", METHOD_TYPE_VOID },
    { MID_ON_PURCHASE, "onPurchase", METHOD_TYPE_VOID },
    { MID_ON_UNLOCK_ACHIEVEMENT, "onUnlockAchievement", METHOD_TYPE_VOID },
    { MID_IS_SIGNED_IN, "isSignedIn", METHOD_TYPE_BOOLEAN },
    { MID_IS_DOWNLOADED, "isDownloaded", METHOD_TYPE_BOOLEAN },
    { MID_IS_DOWNLOADING, "isDownloading", METHOD_TYPE_BOOLEAN },
    { MID_GET_DOWNLOAD_PROGRESS, "getDownloadProgress", METHOD_TYPE_INT },
    { MID_CHECK_LICENSE, "checkLicense", METHOD_TYPE_BOOLEAN },
    { MID_IS_NETWORK_AVAILABLE, "isNetworkAvailable", METHOD_TYPE_BOOLEAN },
};

MethodsBoolean methodsBoolean[] = {
    { MID_CFG_KEY_EXISTS, ConfigKeyExists },
    { MID_CFG_READ_BOOL, ConfigReadBoolean },
    { MID_CFG_WRITE_STRING, ConfigWriteString },
    { MID_CFG_WRITE_INT, ConfigWriteInteger },
    { MID_CFG_WRITE_BOOL, ConfigWriteBoolean },
    { MID_CHECK_PRIVATE_DIR, CheckPrivateDirectoryExists },
    { MID_SET_ACTIVITY_TITLE, SetActivityTitle },
    { MID_HAS_FEATURE, HasFeature },
    { MID_IS_USING_BLUETOOTH, IsUsingBluetooth },
    { MID_CHECK_INIT, CheckInit },
    { MID_SUPPORTS_LOW_LATENCY, SupportsLowLatency },
    { MID_AUDIO_DEVICE_INIT, AudioDeviceInit },
    { MID_IS_DATA_AVAILABLE, IsDataAvailable },
    { MID_IS_TV, IsTV },
    { MID_IS_PURCHASED, IsPurchased },
    { MID_IS_PRODUCT_PURCHASED, IsPurchased },
    { MID_IS_DOWNLOADED, IsDownloaded },
    { MID_IS_DOWNLOADING, IsDownloading },
    { MID_CHECK_LICENSE, CheckLicense },
    { MID_IS_NETWORK_AVAILABLE, IsNetworkAvailable },
    { MID_IS_SIGNED_IN, IsSignedIn },
    { MID_DEV_IS_TABLET, DeviceIsTablet },
    { MID_DEV_IS_KEYBOARD_SHOWING, DeviceIsKeyboardShowing },
    { MID_DEV_IS_SUPPORTED_ORIENTATION, DeviceIsSupportedOrientation },
    { MID_GFX_IS_GLES20, GraphicsIsOpenGLES20 },
    { MID_GFX_CAN_SET_SCALE, GraphicsCanSetScale },
    { MID_UI_PROCESS_EVENTS, UIProcessEvents },
};
MethodsByte methodsByte[] = {};
MethodsChar methodsChar[] = {};
MethodsDouble methodsDouble[] = { { MID_GET_RAM_AMOUNT, GetRamAmount }, };
MethodsFloat methodsFloat[] = { { MID_GET_XDPI, GetXDPI }, { MID_GET_YDPI, GetYDPI }, };
MethodsInt methodsInt[] = {
    { MID_CFG_READ_INT, ConfigReadInteger },
    { MID_AUDIO_INIT, AudioInit },
    { MID_GET_SAMPLE_RATE, GetSampleRate },
    { MID_GET_OUTPUT_FRAMES_PER_BUFFER, GetOutputFramesPerBuffer },
    { MID_GET_WIDTH, GetWidth }, { MID_GET_HEIGHT, GetHeight },
    { MID_GET_SCREEN_WIDTH, GetWidth }, { MID_GET_SCREEN_HEIGHT, GetHeight },
    { MID_GET_SURFACE_WIDTH, GetWidth }, { MID_GET_SURFACE_HEIGHT, GetHeight },
    { MID_GET_OUTPUT_SAMPLE_RATE, GetOutputSampleRate },
    { MID_GET_OUTPUT_BLOCK_SIZE, GetOutputBlockSize },
    { MID_GET_DOWNLOAD_PROGRESS, GetDownloadProgress },
    { MID_DEV_GET_CURRENT_ORIENTATION, DeviceGetCurrentOrientation },
    { MID_INFO_PRODUCT_VERSION_INT, SysGetProductVersionInt },
    { MID_GET_DEVICE_TIER, GetDeviceTier },
    { MID_GET_CPU_CORE_COUNT, GetCpuCoreCount },
    { MID_GFX_SYS_FBO, GraphicsGetSysFBO },
};
MethodsLong methodsLong[] = {
    { MID_RES_GET_FS_BLOCK_COUNT, ResourcesGetFsBlockCount },
    { MID_RES_GET_FS_BLOCK_SIZE, ResourcesGetFsBlockSize },
    { MID_RES_GET_FS_BLOCKS_FREE, ResourcesGetFsBlocksFree },
    { MID_RES_GET_ASSET_FILE_SIZE, ResourcesGetAssetFileSize },
    { MID_DIAG_MEM_TOTAL, DiagGetMemoryTotal },
    { MID_DIAG_MEM_AVAIL, DiagGetMemoryAvailable },
    { MID_DIAG_MEM_USED, DiagGetMemoryUsed },
};
MethodsObject methodsObject[] = {
    { MID_GET_SESSION_ID, GetSessionIdentifier },
    { MID_GET_SYNERGY_ID, GetSynergyId },
    { MID_GET_ANALYTICS_ENV, GetAnalyticsEnvironment },
    { MID_GET_ANALYTICS_APP_NAME, GetAnalyticsApplicationName },
    { MID_GET_APPLICATION_ID, GetApplicationID },
    { MID_GET_REVENUE_ID, GetRevenueIdentifier },
    { MID_INFO_PRODUCT_VERSION, SysGetProductVersionString },
    { MID_GET_APPLICATION_VERSION, GetApplicationVersion },
    { MID_INFO_PACKAGE_NAME, SysGetPackageName },
    { MID_INFO_ACTIVITY_NAME, SysGetActivityName },
    { MID_INFO_USER_LOCALE, SysGetUserLocale },
    { MID_INFO_COUNTRY_CODE, SysGetCountryCode },
    { MID_GET_LANGUAGE, GetLanguage },
    { MID_INFO_CURRENCY_CODE, SysGetUserCurrencyCode },
    { MID_INFO_CURRENCY_SYMBOL, SysGetUserCurrencySymbol },
    { MID_CFG_READ_STRING, ConfigReadString },
    { MID_STR_STORE_GET, StringStoreGet },
    { MID_PRIVATE_FILE_PATH, PrivateFilePath },
    { MID_READ_SHARED_PROP, ReadSharedProperty },
    { MID_GET_EXTERNAL_STORAGE_DIRECTORY, GetExternalStorageDirectory },
    { MID_GET_PACKAGE_NAME, GetPackageName },
    { MID_GET_OBB_FILENAME, GetObbFileName },
    { MID_SYS_GET_MAIN_EXPANSION_PATH, SysGetMainExpansionFilePath },
    { MID_RES_GET_ASSET_FILE_INFO, ResourcesGetAssetFileInfo },
    { MID_GET_EXTERNAL_STORAGE_PATH, GetExternalStoragePath },
    { MID_GET_INTERNAL_STORAGE_PATH, GetInternalStoragePath },
    { MID_UTIL_GET_UUID_STRING, UtilGetUUIDString },
    { MID_GENERATE_UUID, GenerateUUID },
    { MID_GET_ANALYTICS_DEVICE_ID, GetAnalyticsDeviceIdentifier },
    { MID_DIAG_GET_DEVICE_ID, DiagGetDeviceID },
    { MID_RES_GET_RESOURCE_FOLDER, ResourcesGetResourceFolder },
    { MID_RES_GET_USER_DATA_FOLDER, ResourcesGetUserDataFolder },
    { MID_RES_GET_APP_SUPPORT_FOLDER, ResourcesGetAppSupportDataFolder },
    { MID_RES_GET_CACHE_FOLDER, ResourcesGetCacheDataFolder },
    { MID_RES_GET_EXT_STORAGE_DIR, ResourcesGetExtStorageDir },
    { MID_DEV_GET_CACHES_DIR, DeviceGetCachesDir },
    { MID_DEV_GET_DEVICE_NAME, DeviceGetDeviceName },
    { MID_GET_ASSETS, GetAssets },
    { MID_GET_CONTEXT, GetContext },
    { MID_GET_APPLICATION_CONTEXT, GetApplicationContext },
    { MID_GET_NATIVE_SURFACE, GetNativeSurface },
    { MID_INPUT_GET_INPUT_DEVICE_IDS, InputGetInputDeviceIds },
    { MID_GET_HARDWARE_MODEL, GetHardwareModel },
    { MID_GET_HARDWARE_DISPLAY, GetHardwareDisplay },
    { MID_GET_HARDWARE_MANUFACTURER, GetHardwareManufacturer },
    { MID_GET_EXTERNAL_STORAGE_STATE, GetExternalStorageState },
    { MID_GET_EXTERNAL_STORAGE_DIRS, GetExternalStorageDirs },
    { MID_GET_FILES_DIR, GetFilesDir },
    { MID_GET_ABSOLUTE_PATH, GetAbsolutePath },
    { MID_GET_ASSET_MANAGER, GetAssetManager },
    { MID_FMOD_AUDIODEVICE_INIT, FmodAudioDeviceInit },
    { MID_GET_LOCALE, GetLocale },
    { MID_GET_HARDWARE_OS, GetHardwareOS },
    { MID_GET_HARDWARE_BOARD, GetHardwareBoard },
    { MID_GET_PURCHASED_SKUS, GetPurchasedSkus },
    { MID_GET_PURCHASE_PROVIDER, GetPurchaseProvider },
};
MethodsShort methodsShort[] = {};
MethodsVoid methodsVoid[] = {
    { MID_CFG_ERASE_KEY, ConfigEraseKey },
    { MID_STR_STORE_SET, StringStoreSet },
    { MID_FLIP_BUFFERS, FlipBuffers },
    { MID_AUDIO_WRITE_SHORT_BUFFER, AudioWriteShortBuffer },
    { MID_AUDIO_WRITE_BYTE_BUFFER, AudioWriteByteBuffer },
    { MID_AUDIO_QUIT, AudioQuit },
    { MID_POLL_INPUT_DEVICES, PollInputDevices },
    { MID_SET_FRAMEBUFFER_SIZE, SetFramebufferSize },
    { MID_AUDIO_DEVICE_CLOSE, AudioDeviceClose },
    { MID_AUDIO_DEVICE_WRITE, AudioDeviceWrite },
    { MID_UPDATE_PURCHASES, UpdatePurchases },
    { MID_REQUEST_PERMISSION, RequestPermission },
    { MID_PURCHASE, Purchase },
    { MID_ON_PURCHASE, OnPurchase },
    { MID_ON_UNLOCK_ACHIEVEMENT, OnUnlockAchievement },
    { MID_DEV_SHOW_KEYBOARD, DeviceShowKeyboard },
    { MID_DEV_HIDE_KEYBOARD, DeviceHideKeyboard },
    { MID_DEV_SHOW_NUMERIC_KEYBOARD, DeviceShowNumericKeyboard },
    { MID_DEV_EXIT_TO_HOME, DeviceVoidNoop },
    { MID_DEV_KILL_APPLICATION, DeviceVoidNoop },
    { MID_DEV_FORCE_ROTATE, DeviceVoidNoop },
    { MID_GFX_SCREEN_PIXELS, GraphicsGetScreenSize },
    { MID_GFX_SCREEN_POINTS, GraphicsGetScreenSize },
};

/*
 * JNI Fields
 */
char WINDOW_SERVICE[] = "window";
const int SDK_INT = 19;

NameToFieldID nameToFieldId[] = {
    { FID_WINDOW_SERVICE, "WINDOW_SERVICE", FIELD_TYPE_OBJECT },
    { FID_SDK_INT, "SDK_INT", FIELD_TYPE_INT },
    { FID_M_ASSET_MGR, "mAssetMgr", FIELD_TYPE_OBJECT },
};

FieldsBoolean fieldsBoolean[] = {};
FieldsByte fieldsByte[] = {};
FieldsChar fieldsChar[] = {};
FieldsDouble fieldsDouble[] = {};
FieldsFloat fieldsFloat[] = {};
FieldsInt fieldsInt[] = { { FID_SDK_INT, SDK_INT }, };
FieldsObject fieldsObject[] = { { FID_WINDOW_SERVICE, WINDOW_SERVICE }, { FID_M_ASSET_MGR, (jobject)0x1 }, };
FieldsLong fieldsLong[] = {};
FieldsShort fieldsShort[] = {};

static void sync_runtime_field_objects(void) {
    for (size_t i = 0; i < sizeof(fieldsObject) / sizeof(fieldsObject[0]); ++i) {
        if (fieldsObject[i].id == FID_M_ASSET_MGR) { fieldsObject[i].value = g_asset_manager_obj; break; }
    }
}

__FALSOJNI_IMPL_CONTAINER_SIZES
