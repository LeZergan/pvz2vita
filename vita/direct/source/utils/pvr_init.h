/*
 * Copyright (C) 2026
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file pvr_init.h
 * @brief PVR_PSP2 native GPU driver initialization.
 *
 * Replaces vitaGL initialization when USE_PVR_PSP2 is defined.
 * Uses the Imagination Technologies PowerVR SGX driver directly
 * through libgpu_es4_ext, libIMGEGL, and libGLESv2.
 */

#ifndef SOLOADER_PVR_INIT_H
#define SOLOADER_PVR_INIT_H

#include "so_util/so_util.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * so_module objects for PVR .suprx files.
 * Exported so dynlib.c can add them to the symbol-search chain.
 */
extern so_module so_mod_pvr_gpu;
extern so_module so_mod_pvr_egl;
extern so_module so_mod_pvr_pvr2d;
extern so_module so_mod_pvr_gles1;
extern so_module so_mod_pvr_gles2;
extern so_module so_mod_pvr_wsegl;

/**
 * @brief Load required PVR kernel modules (.suprx).
 *
 * Must be called early, before any EGL/GL calls.
 * Loads libgpu_es4_ext.suprx (kernel driver) and
 * libIMGEGL.suprx (EGL implementation).
 */
void pvr_load_modules(void);

/**
 * @brief Initialize PVR apphint and create virtual apphint.
 *
 * Sets up driver memory sizes and module paths for the
 * PVR_PSP2 driver. Must be called after pvr_load_modules()
 * but before any EGL initialization.
 */
void pvr_init_apphint(void);

/**
 * @brief Initialize PVR EGL display, surface, and context.
 *
 * Creates a window surface on the Vita framebuffer and
 * sets up the GLES2 rendering context. Must be called
 * after pvr_init_apphint().
 *
 * @param fb_w  Framebuffer width.
 * @param fb_h  Framebuffer height.
 */
void pvr_init_gl(int fb_w, int fb_h);

/**
 * @brief Bind the already-created PVR EGL context on the current thread.
 *
 * Use this after pvr_init_gl() when another thread needs to issue GLES calls.
 *
 * @return non-zero on success.
 */
int pvr_make_current(void);

/**
 * @brief Bind the PVR EGL context only if it is not known to be owned by
 *        another thread.
 *
 * This is used by opportunistic loading-screen redraws. It avoids stealing
 * the single PVR context from SDL/the game render thread.
 *
 * @return non-zero on success.
 */
int pvr_try_make_current(void);

/**
 * @brief Return non-zero if the PVR EGL context is current on this thread.
 */
int pvr_context_is_current_on_thread(void);

/**
 * @brief Release the PVR EGL context from the current thread.
 *
 * @return non-zero on success.
 */
int pvr_release_current(void);

/**
 * @brief Swap buffers using PVR EGL.
 *
 * Calls eglSwapBuffers on the global PVR display and surface.
 *
 * @return non-zero on success.
 */
int pvr_swap_buffers(void);

/**
 * @brief Global EGL display handle (set after pvr_init_gl).
 */
extern void *g_pvr_egl_display;
extern void *g_pvr_egl_surface;
extern void *g_pvr_egl_context;

#ifdef __cplusplus
}
#endif

#endif /* SOLOADER_PVR_INIT_H */
