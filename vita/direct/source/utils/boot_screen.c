/* Shader-independent setup screen. Allocates display memory only on failure.
 * Framebuffer layout follows VitaSDK samples/common/debugScreen.c. */
#include "utils/boot_check.h"
#include "utils/boot_font.h"
#include <stdint.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/processmgr.h>

static void draw_text(uint32_t *pixels, int x, int y, const char *text, uint32_t color, int bottom) {
    int left = x;
    for (; *text && y + 16 <= bottom; ++text) {
        unsigned ch = (unsigned char)*text;
        if (ch == '\n') { x = left; y += 24; continue; }
        if (x + 16 > 928) { x = left; y += 24; }
        if (y + 16 > bottom) break;
        if (ch < 32 || ch > 126) ch = '?';
        for (int row = 0; row < 8; ++row)
            for (int col = 0; col < 8; ++col)
                if (g_font[(ch - 32) * 8 + row] & (0x80 >> col))
                    for (int dy = 0; dy < 2; ++dy)
                        for (int dx = 0; dx < 2; ++dx)
                            pixels[(y + row * 2 + dy) * 960 + x + col * 2 + dx] = color;
        x += 16;
    }
}

void pvz2_boot_screen(const char *message) {
    SceUID block = sceKernelAllocMemBlock("pvz2_setup_error",
        SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 2 * 1024 * 1024, NULL);
    uint32_t *pixels = NULL;
    if (block >= 0 && sceKernelGetMemBlockBase(block, (void **)&pixels) >= 0) {
        for (unsigned i = 0; i < 960 * 544; ++i) pixels[i] = 0xff251a16;
        draw_text(pixels, 32, 32, "PLANTS VS ZOMBIES 2 - SETUP", 0xff84e4ac, 64);
        draw_text(pixels, 32, 88, message, 0xffeeeeee, 464);
        /* Footer has its own bounds, outside the message area. */
        for (unsigned i = 480 * 960; i < 482 * 960; ++i) pixels[i] = 0xff84e4ac;
        draw_text(pixels, 32, 496, "Press X to close. Fix setup, then relaunch.", 0xffeeeeee, 528);
        SceDisplayFrameBuf frame = {sizeof(frame), pixels, 960,
            SCE_DISPLAY_PIXELFORMAT_A8B8G8R8, 960, 544};
        sceDisplaySetFrameBuf(&frame, SCE_DISPLAY_SETBUF_NEXTFRAME);
    }
    /* Require a new press; held launch buttons must not dismiss the error. */
    unsigned previous = ~0u;
    for (;;) {
        sceDisplayWaitVblankStart();
        SceCtrlData pad = {0};
        if (sceCtrlPeekBufferPositive(0, &pad, 1) > 0) {
            if ((pad.buttons & ~previous) & SCE_CTRL_CROSS) break;
            previous = pad.buttons;
        }
    }
    sceKernelExitProcess(1);
    for (;;) {}
}
