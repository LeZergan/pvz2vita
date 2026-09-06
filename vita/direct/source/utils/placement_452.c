#include "placement_452.h"
#include "telemetry.h"
#include <so_util/so_util.h>

/* See docs/zomboss-crash.md. Only the marker block is replaced; native
 * candidate selection, object positioning and cleanup continue unchanged. */
uintptr_t placement452_resume;

__attribute__((used, noinline))
void placement452_mark(int *cells, int x, int y, int width, int height,
                       int minimum_width, int minimum_height) {
    if (placement452_mark_cells(cells, x, y, width, height,
                                minimum_width, minimum_height)) {
        static unsigned reports;
        if (reports++ < 8)
            telemetry_log("PLACEMENT", "clipped grid footprint origin=%d,%d full=%d,%d minimum=%d,%d",
                          x, y, width, height, minimum_width, minimum_height);
    }
}

/* At libPVZ2+1dca90: r9=grid, r8=x, r11=y; native sp remains in use by
 * the continuation. Save all registers across the C call, keep 8-byte stack
 * alignment, then reproduce the original live-out offset loads/this spill.
 * No code is temporarily unpatched, including on other game threads. */
__attribute__((naked, used, target("arm")))
void placement452_marker_bridge(void) {
    __asm__ volatile(
        "str r6, [sp, #12]\n"
        "push {r0-r12, lr}\n"
        "ldr r3, [sp, #1224]\n" /* 56 + 0x490: full width */
        "ldr r0, [sp, #1228]\n" /* 56 + 0x494: full height */
        "ldr r1, [sp, #1208]\n" /* 56 + 0x480: minimum width */
        "ldr r2, [sp, #1212]\n" /* 56 + 0x484: minimum height */
        "sub sp, sp, #16\n"
        "stm sp, {r0-r2}\n"
        "mov r0, r9\n"
        "mov r1, r8\n"
        "mov r2, r11\n"
        "bl placement452_mark\n"
        "add sp, sp, #16\n"
        "pop {r0-r12, lr}\n"
        "ldr r12, [sp, #1136]\n"
        "ldr lr, [sp, #1140]\n"
        "ldr r0, =placement452_resume\n"
        "ldr pc, [r0]\n"
    );
}

int placement452_install(so_module *module) {
    const uintptr_t base = module->text_base;
    /* Check the entire replaced block plus caller bounds and fallback path.
     * A different library must fail at boot, never receive a guessed patch. */
    static const uint32_t expected[] = {
        0xe1a01009,0xe59d9490,0xe59dc470,0xe59de474,0xe3590001,0xe58d600c,
        0xba00001c,0xe0880108,0xe59d4480,0xe59d6484,0xe3a05000,0xe0810180,
        0xe59d2494,0xe080110b,0xe3520001,0xba00000f,0xe3a00000,0xe1a03001,
        0xea000002,0xe3570001,0xb3007001,0xea000005,0xe5937000,0xe1550004,
        0xb1500006,0xaafffff8,0xe3570003,0xb3007003,0xe2800001,0xe4837004,
        0xe1500002,0xbafffff5,0xe2855001,0xe2811028,0xe1550009,0xbaffffe9
    };
    const uint32_t *code = (const uint32_t *)(base + 0x1dca90);
    for (unsigned i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
        if (code[i] != expected[i]) return -1;
    if (*(uint32_t *)(base + 0x1dc098) != 0xe352000a ||
        *(uint32_t *)(base + 0x1dc0a4) != 0xe3570009 ||
        *(uint32_t *)(base + 0x1dca60) != 0xeb000040 ||
        *(uint32_t *)(base + 0x1dcb20) != 0xe28d7010) return -1;
    placement452_resume = base + 0x1dcb20;
    hook_arm(base + 0x1dca90, (uintptr_t)placement452_marker_bridge);
    telemetry_log("PATCH", "4.5.2 placement grid bounds installed (9x10; relaxed footprint)");
    return 0;
}
