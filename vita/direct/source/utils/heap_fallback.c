#include "heap_fallback.h"
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <stdint.h>
#include <stdatomic.h>
#include <errno.h>
#ifndef PVZ2_HEAP_HOST_TEST
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include "telemetry.h"
#endif

/* A fixed newlib heap cannot use the USER pages left outside it. The supplied
 * dumps abort on 4.5/6 MiB game allocations. Use those pages only AFTER a large
 * heap request fails, without resizing newlib or stealing vitaGL's pools.
 * Keep at least 8 MiB reported free for stacks/kernel allocations, cap total
 * fallback at 16 MiB, and never reserve pages at boot. Actual exhaustion still
 * returns NULL; this is not an unbounded allocator or a leak workaround. */
#define FALLBACK_MIN (256u * 1024u)
#define FALLBACK_MAX (8u * 1024u * 1024u)
#define FALLBACK_BUDGET (16u * 1024u * 1024u)
#define USER_HEADROOM (8u * 1024u * 1024u)
#define FALLBACK_SLOTS 16
#define PAGE_BYTES 4096u
typedef struct { void *ptr; size_t requested, bytes; SceUID id; } FallbackBlock;
static FallbackBlock blocks[FALLBACK_SLOTS];
static size_t reserved_bytes;
static atomic_flag block_lock = ATOMIC_FLAG_INIT;
/* Monotonic bounds reject ordinary heap pointers without a lock or table scan.
 * Entries and accounting are accessed only under block_lock. */
static atomic_uintptr_t low_address = ATOMIC_VAR_INIT(UINTPTR_MAX);
static atomic_uintptr_t high_address;

static void lock_blocks(void) {
    while (atomic_flag_test_and_set_explicit(&block_lock, memory_order_acquire))
        sceKernelDelayThread(50);
}
static void unlock_blocks(void) {
    atomic_flag_clear_explicit(&block_lock, memory_order_release);
}
static int candidate(const void *ptr) {
    uintptr_t at = (uintptr_t)ptr;
    return at >= atomic_load_explicit(&low_address, memory_order_acquire) &&
           at < atomic_load_explicit(&high_address, memory_order_acquire);
}
static int find_locked(const void *ptr) {
    for (int i = 0; i < FALLBACK_SLOTS; ++i)
        if (blocks[i].ptr == ptr) return i;
    return -1;
}
static void *fallback_alloc(size_t alignment, size_t size) {
    if (size < FALLBACK_MIN || size > FALLBACK_MAX || !alignment ||
        alignment > PAGE_BYTES || (alignment & (alignment - 1))) return NULL;
    size_t rounded = (size + PAGE_BYTES - 1) & ~(size_t)(PAGE_BYTES - 1);
    lock_blocks();
    int slot = -1;
    for (int i = 0; i < FALLBACK_SLOTS; ++i)
        if (!blocks[i].ptr) { slot = i; break; }
    SceKernelFreeMemorySizeInfo info = { .size = sizeof(info) };
    if (slot < 0 || rounded > FALLBACK_BUDGET - reserved_bytes ||
        sceKernelGetFreeMemorySize(&info) < 0 || info.size_user < 0 ||
        (size_t)info.size_user < rounded + USER_HEADROOM) {
        unlock_blocks();
        return NULL;
    }
    SceUID id = sceKernelAllocMemBlock("pvz2_heap_fallback",
        SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, rounded, NULL);
    void *ptr = NULL;
    if (id < 0 || sceKernelGetMemBlockBase(id, &ptr) < 0 || !ptr) {
        if (id >= 0) sceKernelFreeMemBlock(id);
        unlock_blocks();
        return NULL;
    }
    blocks[slot] = (FallbackBlock){ptr, size, rounded, id};
    reserved_bytes += rounded;
    if ((uintptr_t)ptr < atomic_load_explicit(&low_address, memory_order_relaxed))
        atomic_store_explicit(&low_address, (uintptr_t)ptr, memory_order_release);
    uintptr_t end = (uintptr_t)ptr + rounded;
    if (end > atomic_load_explicit(&high_address, memory_order_relaxed))
        atomic_store_explicit(&high_address, end, memory_order_release);
    size_t live = reserved_bytes;
    unlock_blocks();
    telemetry_log("HEAP", "large allocation recovered: bytes=%u fallback_live=%u",
                  (unsigned)size, (unsigned)live);
    return ptr;
}

void *pvz2_heap_malloc(size_t size) {
    void *ptr = malloc(size);
    return ptr ? ptr : fallback_alloc(sizeof(void *), size);
}
void *pvz2_heap_memalign(size_t alignment, size_t size) {
    void *ptr = memalign(alignment, size);
    return ptr ? ptr : fallback_alloc(alignment, size);
}
void *pvz2_heap_calloc(size_t count, size_t size) {
    if (count && size > SIZE_MAX / count) { errno = ENOMEM; return NULL; }
    void *ptr = calloc(count, size);
    if (!ptr) {
        ptr = fallback_alloc(sizeof(void *), count * size);
        if (ptr) memset(ptr, 0, count * size);
    }
    return ptr;
}
void pvz2_heap_free(void *ptr) {
    if (!ptr) return;
    if (candidate(ptr)) {
        lock_blocks();
        int slot = find_locked(ptr);
        if (slot >= 0) {
            /* Do not forget a block if the kernel refuses its release. */
            if (sceKernelFreeMemBlock(blocks[slot].id) >= 0) {
                reserved_bytes -= blocks[slot].bytes;
                blocks[slot] = (FallbackBlock){0};
            }
            unlock_blocks();
            return;
        }
        unlock_blocks();
    }
    free(ptr);
}
void *pvz2_heap_realloc(void *ptr, size_t size) {
    if (!ptr) return pvz2_heap_malloc(size);
    if (!size) { pvz2_heap_free(ptr); return NULL; }
    size_t old_size = 0;
    int fallback = 0;
    if (candidate(ptr)) {
        lock_blocks();
        int slot = find_locked(ptr);
        if (slot >= 0) {
            fallback = 1;
            old_size = blocks[slot].requested;
            if (size <= blocks[slot].bytes) {
                blocks[slot].requested = size;
                unlock_blocks();
                return ptr;
            }
        }
        unlock_blocks();
    }
    void *next;
    if (!fallback) {
        next = realloc(ptr, size);
        if (next) return next;
        next = fallback_alloc(sizeof(void *), size);
        if (!next) return NULL; /* realloc failure preserves the old block. */
        old_size = malloc_usable_size(ptr);
    } else {
        next = pvz2_heap_malloc(size);
        if (!next) return NULL;
    }
    memcpy(next, ptr, old_size < size ? old_size : size);
    pvz2_heap_free(ptr);
    return next;
}
