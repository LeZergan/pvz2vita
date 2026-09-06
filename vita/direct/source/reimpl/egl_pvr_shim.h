/*
 * Copyright (C) 2026
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  egl_pvr_shim.h
 * @brief PVR-backed EGL shim.
 *
 * When USE_PVR_PSP2 is defined, the game (and SDL2) import the standard
 * egl* entry points through dynlib. If those resolve directly to the real
 * PVR libIMGEGL, SDL drives EGL with an Android-style flow (fake native
 * window from getNativeSurface) and fails with "Could not initialize EGL",
 * killing the game before it loads anything.
 *
 * Instead, dynlib points the game's egl* imports at these pvrshim_* wrappers.
 * They hand back the single real PVR display/surface/context that
 * pvr_init_gl() already created (with a Vita default fullscreen window), so
 * SDL's window/context creation "succeeds" and shares our PVR context.
 * Real PVR egl* are still called directly by pvr_init.c / loading_screen.c.
 */

#ifndef SOLOADER_EGL_PVR_SHIM_H
#define SOLOADER_EGL_PVR_SHIM_H

#ifdef USE_PVR_PSP2

#include <EGL/egl.h>

#ifdef __cplusplus
extern "C" {
#endif

EGLDisplay pvrshim_eglGetDisplay(EGLNativeDisplayType display_id);
EGLBoolean pvrshim_eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor);
EGLBoolean pvrshim_eglBindAPI(EGLenum api);
EGLBoolean pvrshim_eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list,
                                   EGLConfig *configs, EGLint config_size,
                                   EGLint *num_config);
EGLBoolean pvrshim_eglGetConfigs(EGLDisplay dpy, EGLConfig *configs,
                                 EGLint config_size, EGLint *num_config);
EGLBoolean pvrshim_eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config,
                                      EGLint attribute, EGLint *value);
EGLContext pvrshim_eglCreateContext(EGLDisplay dpy, EGLConfig config,
                                    EGLContext share_context,
                                    const EGLint *attrib_list);
EGLSurface pvrshim_eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                          EGLNativeWindowType win,
                                          const EGLint *attrib_list);
EGLBoolean pvrshim_eglMakeCurrent(EGLDisplay dpy, EGLSurface draw,
                                  EGLSurface read, EGLContext ctx);
EGLBoolean pvrshim_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface);
EGLBoolean pvrshim_eglSwapInterval(EGLDisplay dpy, EGLint interval);
EGLDisplay pvrshim_eglGetCurrentDisplay(void);
EGLContext pvrshim_eglGetCurrentContext(void);
EGLint     pvrshim_eglGetError(void);
EGLBoolean pvrshim_eglQuerySurface(EGLDisplay dpy, EGLSurface surface,
                                   EGLint attribute, EGLint *value);
EGLBoolean pvrshim_eglQueryContext(EGLDisplay dpy, EGLContext ctx,
                                   EGLint attribute, EGLint *value);
char const *pvrshim_eglQueryString(EGLDisplay dpy, EGLint name);
void      (*pvrshim_eglGetProcAddress(const char *procname))(void);
EGLBoolean pvrshim_eglDestroyContext(EGLDisplay dpy, EGLContext ctx);
EGLBoolean pvrshim_eglDestroySurface(EGLDisplay dpy, EGLSurface surface);
EGLBoolean pvrshim_eglTerminate(EGLDisplay dpy);
EGLBoolean pvrshim_eglWaitNative(EGLint engine);
EGLBoolean pvrshim_eglWaitGL(void);

#ifdef __cplusplus
}
#endif

#endif /* USE_PVR_PSP2 */

#endif /* SOLOADER_EGL_PVR_SHIM_H */
