/* Port-specific CPU texture preparation. No graphics calls on these workers. */
#include "utils/pixel_workers.h"
#include "utils/etc1_block.h"
#include "reimpl/pthr.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdatomic.h>
#ifndef PVZ2_PIXEL_HOST_TEST
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/error.h>
#include "utils/telemetry.h"
#endif

#define PIXEL_MAX_WORKERS 3
#define PIXEL_PARALLEL_MIN (256u * 256u)
typedef struct {
    const uint8_t *src;
    uint8_t *dst;
    int width, out_width, out_height;
    enum pvz2_pixel_mode mode;
} PixelJob;
static pthread_mutex_t pixel_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t submit_lock = PTHREAD_MUTEX_INITIALIZER;
static SceUID worker_start[PIXEL_MAX_WORKERS], completion;
static unsigned worker_count;
static atomic_uint pending, generation;
static atomic_uint next_row;
static int initialized;
static PixelJob current_job;
static uint64_t jobs, parallel_jobs, worker_pixels, caller_pixels;

/* Notifications only shorten the sleep. The published generation and pending
 * count are authoritative, so a coalesced/missing wake cannot strand a job.
 * Never return a buffer while a worker still owns its rows. */
static void pixel_wait(SceUID notification, unsigned timeout) {
    int rc = sceKernelWaitSema(notification, 1, &timeout);
    if (rc < 0 && rc != SCE_KERNEL_ERROR_WAIT_TIMEOUT)
        sceKernelDelayThread(1000);
}

/* Keep the large ETC decoder's stack/register needs out of the cheaper
 * alpha and RGBA kernels. Dispatch once per row range, not once per pixel. */
static __attribute__((noinline)) void pixel_etc_rows(const PixelJob *job, int first, int end) {
    int half = job->mode == PVZ2_ETC1_HALF;
    int rows = half ? 2 : 4, bw = (job->width + 3) / 4;
    for (int by = first / rows; by < (end + rows - 1) / rows; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            uint8_t block[64];
            pvz2_etc1_block(job->src + ((size_t)by * bw + bx) * 8, block);
            if (!half && bx * 4 + 4 <= job->out_width &&
                by * 4 >= first && by * 4 + 4 <= end) {
                for (int y = 0; y < 4; ++y)
                    memcpy(job->dst + ((size_t)(by * 4 + y) * job->out_width + bx * 4) * 4,
                           block + y * 16, 16);
                continue;
            }
            for (int y = 0; y < rows; ++y) {
                int py = by * rows + y;
                if (py < first || py >= end) continue;
                for (int x = 0; x < rows; ++x) {
                    int px = bx * rows + x;
                    if (px >= job->out_width) continue;
                    uint8_t *dst = job->dst + ((size_t)py * job->out_width + px) * 4;
                    const uint8_t *p = block + (y * (half ? 2 : 1) * 4 + x * (half ? 2 : 1)) * 4;
                    for (int c = 0; c < 4; ++c)
                        dst[c] = half ? (p[c] + p[c+4] + p[c+16] + p[c+20]) / 4 : p[c];
                }
            }
        }
    }
}

static __attribute__((noinline)) void pixel_rgba_rows(const PixelJob *job, int first, int end) {
    for (int y = first; y < end; ++y) {
        uint8_t *dst = job->dst + (size_t)y * job->out_width * 4u;
        const uint8_t *row = job->src + (size_t)y * 2u * job->width * 4u;
        for (int x = 0; x < job->out_width; ++x, dst += 4) {
            const uint8_t *a = row + (size_t)x * 8u;
            const uint8_t *b = a + (size_t)job->width * 4u;
            /* Two independent 16-bit lanes hold four-byte channel sums
             * without carry between channels. memcpy permits unaligned
             * source and caller-owned destination buffers on ARM. */
            uint32_t a0, a1, b0, b1;
            memcpy(&a0, a, 4); memcpy(&a1, a + 4, 4);
            memcpy(&b0, b, 4); memcpy(&b1, b + 4, 4);
            const uint32_t mask = UINT32_C(0x00ff00ff);
            uint32_t lo = ((a0 & mask) + (a1 & mask) + (b0 & mask) + (b1 & mask)) >> 2;
            uint32_t hi = (((a0 >> 8) & mask) + ((a1 >> 8) & mask) +
                           ((b0 >> 8) & mask) + ((b1 >> 8) & mask)) >> 2;
            uint32_t pixel = (lo & mask) | ((hi & mask) << 8);
            memcpy(dst, &pixel, 4);
        }
    }
}

static inline void pixel_store_alpha(uint8_t *dst, unsigned alpha) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint32_t pixel = UINT32_C(0x00ffffff) | ((uint32_t)alpha << 24);
#else
    uint32_t pixel = UINT32_C(0xffffff00) | alpha;
#endif
    memcpy(dst, &pixel, 4);
}

static void pixel_rows(const PixelJob *job, int first, int end) {
    if (job->mode >= PVZ2_ETC1_RGBA) { pixel_etc_rows(job, first, end); return; }
    if (job->mode == PVZ2_RGBA_HALF) { pixel_rgba_rows(job, first, end); return; }
    const int half = job->mode != PVZ2_ALPHA_RGBA;
    for (int y = first; y < end; ++y) {
        uint8_t *dst = job->dst + (size_t)y * job->out_width * 4u;
        const uint8_t *row = job->src ? job->src +
            (size_t)y * (half ? 2u : 1u) * job->width : NULL;
        if (!row) {
            for (int x = 0; x < job->out_width; ++x, dst += 4) {
                pixel_store_alpha(dst, 0);
            }
        } else if (!half) {
            for (int x = 0; x < job->out_width; ++x, dst += 4) {
                pixel_store_alpha(dst, row[x]);
            }
        } else {
            for (int x = 0; x < job->out_width; ++x, dst += 4) {
                const uint8_t *a = row + (size_t)x * 2u;
                const uint8_t *b = a + job->width;
                pixel_store_alpha(dst, (a[0] + a[1] + b[0] + b[1]) >> 2);
            }
        }
    }
}

/* All participants claim aligned strips from one queue. A delayed helper owns
 * no reserved slice, so the caller and other helper can drain the remaining
 * work. Four-row alignment also avoids decoding an ETC block twice. */
static unsigned pixel_claim_rows(const PixelJob *job) {
    unsigned done = 0;
    for (;;) {
        unsigned first = atomic_fetch_add_explicit(&next_row, 16, memory_order_relaxed);
        if (first >= (unsigned)job->out_height) return done;
        unsigned end = first + 16;
        if (end > (unsigned)job->out_height) end = (unsigned)job->out_height;
        pixel_rows(job, (int)first, (int)end);
        done += end - first;
    }
}

static void *pixel_worker(void *arg) {
    const unsigned index = (unsigned)(uintptr_t)arg;
#ifndef PVZ2_PIXEL_HOST_TEST
    const SceUID self = sceKernelGetThreadId();
    /* pthread_create_soloader already enables all three application cores. */
    telemetry_log("PIXELS", "worker=%u tid=0x%x mask=0x%x", index, self,
                  sceKernelGetThreadCpuAffinityMask(self));
#endif
    unsigned seen = 0;
    for (;;) {
        unsigned next;
        while ((next = atomic_load_explicit(&generation, memory_order_acquire)) == seen)
            pixel_wait(worker_start[index], 100000);
        seen = next;
        const PixelJob job = current_job;
        pixel_claim_rows(&job);
        /* Acquire/release RMWs combine every worker's completed pixel writes.
         * After this decrement, do not touch current_job or its buffers. */
        atomic_fetch_sub_explicit(&pending, 1, memory_order_acq_rel);
        sceKernelSignalSema(completion, 1);
    }
    return NULL;
}

void pvz2_pixels_init(unsigned workers) {
    pthread_mutex_lock(&pixel_lock);
    if (!initialized) {
        initialized = 1;
        if (workers > PIXEL_MAX_WORKERS) workers = PIXEL_MAX_WORKERS;
        completion = sceKernelCreateSema("pvz2_pixel_done", 0, 0, PIXEL_MAX_WORKERS, NULL);
        if (completion < 0) workers = 0;
        for (; worker_count < workers; ++worker_count) {
            pthread_t thread;
            worker_start[worker_count] = sceKernelCreateSema("pvz2_pixel_start", 0, 0, 1, NULL);
            if (worker_start[worker_count] < 0) break;
            if (pthread_create_soloader(&thread, NULL, pixel_worker,
                                        (void *)(uintptr_t)worker_count) != 0) {
                sceKernelDeleteSema(worker_start[worker_count]);
                break;
            }
            pthread_detach(thread);
        }
        if (!worker_count && completion >= 0) sceKernelDeleteSema(completion);
#ifndef PVZ2_PIXEL_HOST_TEST
        telemetry_log("PIXELS", "kernel notifications + atomic completion; workers=%u", worker_count);
#endif
    }
    pthread_mutex_unlock(&pixel_lock);
}

int pvz2_pixels_convert_into(const uint8_t *src, int width, int height,
                            enum pvz2_pixel_mode mode, uint8_t *dst, size_t capacity) {
    if (!dst || width < 1 || height < 1 || mode < PVZ2_RGBA_HALF || mode > PVZ2_ETC1_HALF ||
        ((mode == PVZ2_RGBA_HALF || mode >= PVZ2_ETC1_RGBA) && !src)) return 0;
    if (mode >= PVZ2_ETC1_RGBA && (width > 4096 || height > 4096)) return 0;
    const int half = mode != PVZ2_ALPHA_RGBA && mode != PVZ2_ETC1_RGBA;
    const int dw = half ? width / 2 : width, dh = half ? height / 2 : height;
    if (!dw || !dh || (size_t)width > SIZE_MAX / (size_t)height / 4u ||
        capacity < (size_t)dw * dh * 4u) return 0;
    PixelJob job = {src, dst, width, dw, dh, mode};
    unsigned caller_rows = (unsigned)dh;
    const int parallel = worker_count && (unsigned)dh > worker_count &&
                         (size_t)dw * dh >= PIXEL_PARALLEL_MIN &&
                         pthread_mutex_trylock(&submit_lock) == 0;
    if (parallel) {
        current_job = job;
        atomic_store_explicit(&next_row, 0, memory_order_relaxed);
        atomic_store_explicit(&pending, worker_count, memory_order_relaxed);
        atomic_fetch_add_explicit(&generation, 1, memory_order_release);
        for (unsigned i = 0; i < worker_count; ++i)
            sceKernelSignalSema(worker_start[i], 1);
    }
    if (parallel) caller_rows = pixel_claim_rows(&job);
    else pixel_rows(&job, 0, dh);
    if (parallel) {
        while (atomic_load_explicit(&pending, memory_order_acquire))
            pixel_wait(completion, 10000);
    }
    pthread_mutex_lock(&pixel_lock);
    if (parallel) ++parallel_jobs;
    ++jobs;
    worker_pixels += (uint64_t)((unsigned)dh - caller_rows) * dw;
    caller_pixels += (uint64_t)caller_rows * dw;
    pthread_mutex_unlock(&pixel_lock);
    if (parallel) pthread_mutex_unlock(&submit_lock);
    return 1;
}

uint8_t *pvz2_pixels_convert(const uint8_t *src, int width, int height,
                            enum pvz2_pixel_mode mode) {
    if (width < 1 || height < 1 || mode < PVZ2_RGBA_HALF || mode > PVZ2_ETC1_HALF ||
        (size_t)width > SIZE_MAX / (size_t)height / 4u) return NULL;
    if ((mode == PVZ2_RGBA_HALF || mode >= PVZ2_ETC1_RGBA) && !src) return NULL;
    if (mode >= PVZ2_ETC1_RGBA && (width > 4096 || height > 4096)) return NULL;
    int half = mode != PVZ2_ALPHA_RGBA && mode != PVZ2_ETC1_RGBA;
    size_t bytes = (size_t)(half ? width / 2 : width) * (half ? height / 2 : height) * 4u;
    if (!bytes) return NULL;
    uint8_t *dst = malloc(bytes);
    if (!dst) return NULL;
    if (!pvz2_pixels_convert_into(src, width, height, mode, dst, bytes)) { free(dst); return NULL; }
    return dst;
}

void pvz2_pixels_format_stats(char *out, size_t size) {
    pthread_mutex_lock(&pixel_lock);
    snprintf(out, size, "workers=%u jobs=%llu parallel=%llu worker_px=%llu caller_px=%llu",
             worker_count, (unsigned long long)jobs, (unsigned long long)parallel_jobs,
             (unsigned long long)worker_pixels, (unsigned long long)caller_pixels);
    jobs = parallel_jobs = worker_pixels = caller_pixels = 0;
    pthread_mutex_unlock(&pixel_lock);
}
