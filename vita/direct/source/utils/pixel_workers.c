/* Port-specific CPU texture preparation. No graphics calls on these workers. */
#include "utils/pixel_workers.h"
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

static void pixel_rows(const PixelJob *job, int first, int end) {
    const int half = job->mode != PVZ2_ALPHA_RGBA;
    const int rgba = job->mode == PVZ2_RGBA_HALF;
    for (int y = first; y < end; ++y) {
        uint8_t *dst = job->dst + (size_t)y * job->out_width * 4u;
        const uint8_t *row = job->src ? job->src +
            (size_t)y * (half ? 2u : 1u) * job->width * (rgba ? 4u : 1u) : NULL;
        for (int x = 0; x < job->out_width; ++x, dst += 4) {
            if (rgba) {
                const uint8_t *a = row + (size_t)x * 8u;
                const uint8_t *b = a + (size_t)job->width * 4u;
                for (int c = 0; c < 4; ++c)
                    dst[c] = (uint8_t)((a[c] + a[c+4] + b[c] + b[c+4]) >> 2);
            } else {
                dst[0] = dst[1] = dst[2] = 255;
                if (!row) dst[3] = 0;
                else if (!half) dst[3] = row[x];
                else {
                    const uint8_t *a = row + (size_t)x * 2u;
                    const uint8_t *b = a + job->width;
                    dst[3] = (uint8_t)((a[0] + a[1] + b[0] + b[1]) >> 2);
                }
            }
        }
    }
}

static void *pixel_worker(void *arg) {
    const unsigned index = (unsigned)(uintptr_t)arg;
#ifndef PVZ2_PIXEL_HOST_TEST
    const SceUID self = sceKernelGetThreadId();
    const int previous = sceKernelGetThreadCpuAffinityMask(self);
    const int wanted = 0x10000 << (index + 1);
    /* Each helper gets a distinct available core; retain the verified shared
     * worker mask if the kernel refuses an individual-core assignment. */
    if (previous > 0 && (previous & wanted)) {
        if (sceKernelChangeThreadCpuAffinityMask(self, wanted) < 0 ||
            sceKernelGetThreadCpuAffinityMask(self) != wanted)
            sceKernelChangeThreadCpuAffinityMask(self, previous);
    }
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
        const int first = (int)((uint64_t)job.out_height * index / (worker_count + 1));
        const int end = (int)((uint64_t)job.out_height * (index + 1) / (worker_count + 1));
        pixel_rows(&job, first, end);
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

uint8_t *pvz2_pixels_convert(const uint8_t *src, int width, int height,
                            enum pvz2_pixel_mode mode) {
    if (width < 1 || height < 1 || mode < PVZ2_RGBA_HALF || mode > PVZ2_ALPHA_HALF ||
        (mode == PVZ2_RGBA_HALF && !src)) return NULL;
    const int half = mode != PVZ2_ALPHA_RGBA;
    const int dw = half ? width / 2 : width, dh = half ? height / 2 : height;
    if (!dw || !dh || (size_t)width > SIZE_MAX / (size_t)height / 4u) return NULL;
    uint8_t *dst = malloc((size_t)dw * dh * 4u);
    if (!dst) return NULL;
    PixelJob job = {src, dst, width, dw, dh, mode};
    int first = 0;
    const int parallel = worker_count && (unsigned)dh > worker_count &&
                         (size_t)dw * dh >= PIXEL_PARALLEL_MIN &&
                         pthread_mutex_trylock(&submit_lock) == 0;
    if (parallel) {
        current_job = job;
        atomic_store_explicit(&pending, worker_count, memory_order_relaxed);
        atomic_fetch_add_explicit(&generation, 1, memory_order_release);
        first = (int)((uint64_t)dh * worker_count / (worker_count + 1));
        for (unsigned i = 0; i < worker_count; ++i)
            sceKernelSignalSema(worker_start[i], 1);
    }
    pixel_rows(&job, first, dh);
    if (parallel) {
        while (atomic_load_explicit(&pending, memory_order_acquire))
            pixel_wait(completion, 10000);
    }
    pthread_mutex_lock(&pixel_lock);
    if (parallel) ++parallel_jobs;
    ++jobs;
    worker_pixels += (uint64_t)first * dw;
    caller_pixels += (uint64_t)(dh - first) * dw;
    pthread_mutex_unlock(&pixel_lock);
    if (parallel) pthread_mutex_unlock(&submit_lock);
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
