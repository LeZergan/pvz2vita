/*
 * Copyright (C) 2026 Ellie J Turner
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/telemetry.h"
#include "utils/bounded_log.h"
#include <pthread.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>

#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>

static const char *k_trace_paths[] = {
    DATA_PATH "loader.log",
};
static pthread_mutex_t g_trace_mutex = PTHREAD_MUTEX_INITIALIZER;

static int g_success_count = 0;
static const char *g_last_path = NULL;
static const char *g_active_path = NULL;

/* Single producer (frame loop), single consumer (transition observer). A slow
 * or failed card must never make the renderer wait for periodic diagnostics.
 * Monotonic unsigned indices also work across counter wraparound. */
#define REPORT_SLOTS 2u
static char g_reports[REPORT_SLOTS][1700];
static atomic_uint g_report_write, g_report_read;
static unsigned g_reports_dropped;
static int g_reports_enabled;

void telemetry_reports_enable(void) { g_reports_enabled = 1; }

void telemetry_report(const char *tag, const char *fmt, ...) {
    if (!g_reports_enabled) return;
    unsigned write = atomic_load_explicit(&g_report_write, memory_order_relaxed);
    unsigned read = atomic_load_explicit(&g_report_read, memory_order_acquire);
    if (write - read == REPORT_SLOTS) { ++g_reports_dropped; return; }
    char *line = g_reports[write % REPORT_SLOTS];
    int prefix = snprintf(line, sizeof(g_reports[0]), "[%s] ", tag ? tag : "TRACE");
    if (prefix < 0 || (unsigned)prefix >= sizeof(g_reports[0]) - 64) return;
    va_list list;
    va_start(list, fmt);
    vsnprintf(line + prefix, sizeof(g_reports[0]) - prefix - 64, fmt, list);
    va_end(list);
    size_t length = strlen(line);
    snprintf(line + length, sizeof(g_reports[0]) - length,
             "\n[REPORTQ] dropped=%u\n", g_reports_dropped);
    atomic_store_explicit(&g_report_write, write + 1, memory_order_release);
}

void telemetry_reports_drain(void) {
    /* At most two writes per wake; never starve the observer on backlog. */
    for (unsigned i = 0; i < REPORT_SLOTS; ++i) {
        unsigned read = atomic_load_explicit(&g_report_read, memory_order_relaxed);
        if (read == atomic_load_explicit(&g_report_write, memory_order_acquire)) return;
        if (!telemetry_try_line(g_reports[read % REPORT_SLOTS])) return;
        atomic_store_explicit(&g_report_read, read + 1, memory_order_release);
    }
}

static int trace_write_one(const char *path, const char *line, int flags) {
    int wrote = 0;
    SceUID fd = sceIoOpen(path, flags, 0644);
    if (fd >= 0) {
        if (line) {
            bounded_log_write(&fd, path, line, sceClibStrnlen(line, 2048));
        }
        sceIoClose(fd);
        wrote = 1;
    } else if (!line && (flags & SCE_O_TRUNC)) {
        FILE *f = fopen(path, "wb");
        if (f) {
            fclose(f);
            wrote = 1;
        }
    }

    if (wrote) {
        g_success_count++;
        g_last_path = path;
    }
    return wrote;
}

static int trace_write_active(const char *line, int flags) {
    if (g_active_path) {
        if (trace_write_one(g_active_path, line, flags)) {
            return 1;
        }
        g_active_path = NULL;
    }

    for (int i = 0; i < (int)(sizeof(k_trace_paths) / sizeof(k_trace_paths[0])); ++i) {
        if (trace_write_one(k_trace_paths[i], line, flags)) {
            g_active_path = k_trace_paths[i];
            return 1;
        }
    }

    return 0;
}

void telemetry_reset(void) {
    g_success_count = 0;
    g_last_path = NULL;
    g_active_path = NULL;
    trace_write_active(NULL, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC);
    trace_write_active("[BOOT] telemetry reset\n", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND);
}

void telemetry_log(const char *tag, const char *fmt, ...) {
    char msg[1536];
    char line[1700];

    va_list list;
    va_start(list, fmt);
    sceClibVsnprintf(msg, sizeof(msg), fmt, list);
    va_end(list);

    sceClibSnprintf(line, sizeof(line), "[%s] %s\n", tag ? tag : "TRACE", msg);
    pthread_mutex_lock(&g_trace_mutex);
    sceClibPrintf("%s", line);
    trace_write_active(line, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND);
    pthread_mutex_unlock(&g_trace_mutex);
}

int telemetry_success_count(void) {
    return g_success_count;
}

int telemetry_try_line(const char *line) {
    if (!line || pthread_mutex_trylock(&g_trace_mutex) != 0) return 0;
    int result = trace_write_active(line, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND);
    pthread_mutex_unlock(&g_trace_mutex);
    return result;
}

const char *telemetry_last_path(void) {
    return g_last_path;
}
