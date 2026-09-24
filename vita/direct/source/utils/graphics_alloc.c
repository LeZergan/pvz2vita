/* Pinned VitaGL draw routines copy into allocations without checking NULL.
 * Keep native pool fallback/GC recovery intact, then stop before that copy if
 * all allocation attempts failed. Texture upload callers retain NULL recovery.
 * The ARM regression verifies these exact native ranges and the real callsite. */
#include <stddef.h>
#include <stdint.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

extern void * __real_gpu_alloc_mapped_aligned(size_t, size_t, int);
extern void glDrawArrays(void), glDrawElements(void);
extern void glDrawArraysInstanced(void), glDrawElementsInstanced(void);
extern void _glDrawArrays_CustomShadersIMPL(void), _glDrawElements_CustomShadersIMPL(void);

void *__wrap_gpu_alloc_mapped_aligned(size_t alignment, size_t size, int pool) {
    void *result = __real_gpu_alloc_mapped_aligned(alignment, size, pool);
    if (result || !size) return result;
    uintptr_t caller = (uintptr_t)__builtin_return_address(0) & ~(uintptr_t)1;
    static const struct { void (*entry)(void); size_t size; } draws[] = {
        {glDrawArrays, 0x206}, {glDrawElements, 0x10e8},
        {_glDrawArrays_CustomShadersIMPL, 0x10ac}, {_glDrawElements_CustomShadersIMPL, 0x1546},
        {glDrawArraysInstanced, 0x270}, {glDrawElementsInstanced, 0xaf0}
    };
    for (unsigned i = 0; i < sizeof(draws) / sizeof(draws[0]); ++i) {
        uintptr_t start = (uintptr_t)draws[i].entry & ~(uintptr_t)1;
        if (caller > start && caller < start + draws[i].size) {
            /* Neither render a dialog through exhausted graphics memory nor
             * block on storage writes while trying to terminate safely. */
            sceClibPrintf("[FATAL] draw allocation exhausted: bytes=%u pool=%d caller=0x%x; exit before NULL copy\n",
                          (unsigned)size, pool, (unsigned)caller);
            sceKernelExitProcess(7);
            for (;;) sceKernelDelayThread(100000);
        }
    }
    return result;
}
