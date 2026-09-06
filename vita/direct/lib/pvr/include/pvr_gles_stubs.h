/*
 * Copyright (C) 2026
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  pvr_gles_stubs.h
 * @brief PVR_PSP2 GLES1 / extension function stubs.
 *
 * Under PVR_PSP2 we use the real EGL/GLES2 headers from the SDK.
 * However the symbol table in dynlib.c references many GLES1 and
 * extension functions that are not declared by <GLES2/gl2.h>.
 * This header provides the missing declarations so the compiler
 * can take their addresses for the default_dynlib table.
 *
 * The actual definitions are provided by the real PVR .suprx
 * modules at runtime (for GLES2 core) or by pvr_gles_stubs.c
 * (for GLES1/OES/EXT no-op stubs).
 */

#ifndef SOLOADER_PVR_GLES_STUBS_H
#define SOLOADER_PVR_GLES_STUBS_H

#ifdef USE_PVR_PSP2

#define GL_GLEXT_PROTOTYPES 1
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------
 * GLES1 types not defined in GLES2 headers
 * ------------------------------------------------------------------ */
typedef GLfixed GLclampx;
typedef GLclampf GLclampd;  /* Not actually used but defined for completeness */

/* ------------------------------------------------------------------
 * GLES1 fixed-function stubs (not available in GLES2)
 * ------------------------------------------------------------------ */

void glAlphaFunc(GLenum func, GLclampf ref);
void glAlphaFuncx(GLenum func, GLclampx ref);
void glClearColorx(GLclampx red, GLclampx green, GLclampx blue, GLclampx alpha);
void glClearDepthx(GLclampx depth);
void glClientActiveTexture(GLenum texture);
void glClipPlanef(GLenum plane, const GLfloat *equation);
void glClipPlanex(GLenum plane, const GLfixed *equation);
void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha);
void glColor4x(GLfixed red, GLfixed green, GLfixed blue, GLfixed alpha);
void glColorPointer(GLint size, GLenum type, GLsizei stride, const void *pointer);
void glDepthRangex(GLclampx zNear, GLclampx zFar);
void glDisableClientState(GLenum array);
void glEnableClientState(GLenum array);
void glFogf(GLenum pname, GLfloat param);
void glFogfv(GLenum pname, const GLfloat *params);
void glFogx(GLenum pname, GLfixed param);
void glFogxv(GLenum pname, const GLfixed *params);
void glFrustumf(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar);
void glFrustumx(GLfixed left, GLfixed right, GLfixed bottom, GLfixed top, GLfixed zNear, GLfixed zFar);
void glGetClipPlanef(GLenum plane, GLfloat *equation);
void glGetClipPlanex(GLenum plane, GLfixed *equation);
void glGetFixedv(GLenum pname, GLfixed *params);
void glGetLightfv(GLenum light, GLenum pname, GLfloat *params);
void glGetLightxv(GLenum light, GLenum pname, GLfixed *params);
void glGetMaterialfv(GLenum face, GLenum pname, GLfloat *params);
void glGetMaterialxv(GLenum face, GLenum pname, GLfixed *params);
void glGetPointerv(GLenum pname, void **params);
void glGetTexEnvfv(GLenum env, GLenum pname, GLfloat *params);
void glGetTexEnviv(GLenum env, GLenum pname, GLint *params);
void glGetTexEnvxv(GLenum env, GLenum pname, GLfixed *params);
void glGetTexParameterxv(GLenum target, GLenum pname, GLfixed *params);
void glLightModelf(GLenum pname, GLfloat param);
void glLightModelfv(GLenum pname, const GLfloat *params);
void glLightModelx(GLenum pname, GLfixed param);
void glLightModelxv(GLenum pname, const GLfixed *params);
void glLightf(GLenum light, GLenum pname, GLfloat param);
void glLightfv(GLenum light, GLenum pname, const GLfloat *params);
void glLightx(GLenum light, GLenum pname, GLfixed param);
void glLightxv(GLenum light, GLenum pname, const GLfixed *params);
void glLineWidth(GLfloat width);
void glLoadIdentity(void);
void glLoadMatrixf(const GLfloat *m);
void glLoadMatrixx(const GLfixed *m);
void glLogicOp(GLenum opcode);
void glMaterialf(GLenum face, GLenum pname, GLfloat param);
void glMaterialfv(GLenum face, GLenum pname, const GLfloat *params);
void glMaterialx(GLenum face, GLenum pname, GLfixed param);
void glMaterialxv(GLenum face, GLenum pname, const GLfixed *params);
void glMatrixMode(GLenum mode);
void glMultMatrixf(const GLfloat *m);
void glMultMatrixx(const GLfixed *m);
void glMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q);
void glMultiTexCoord4x(GLenum target, GLfixed s, GLfixed t, GLfixed r, GLfixed q);
void glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz);
void glNormalPointer(GLenum type, GLsizei stride, const void *pointer);
void glOrthof(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar);
void glOrthox(GLfixed left, GLfixed right, GLfixed bottom, GLfixed top, GLfixed zNear, GLfixed zFar);
void glPointParameterf(GLenum pname, GLfloat param);
void glPointParameterfv(GLenum pname, const GLfloat *params);
void glPointParameterx(GLenum pname, GLfixed param);
void glPointParameterxv(GLenum pname, const GLfixed *params);
void glPointSize(GLfloat size);
void glPopMatrix(void);
void glPushMatrix(void);
void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void glRotatex(GLfixed angle, GLfixed x, GLfixed y, GLfixed z);
void glScalef(GLfloat x, GLfloat y, GLfloat z);
void glScalex(GLfixed x, GLfixed y, GLfixed z);
void glShadeModel(GLenum mode);
void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *pointer);
void glTexEnvf(GLenum target, GLenum pname, GLfloat param);
void glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params);
void glTexEnvi(GLenum target, GLenum pname, GLint param);
void glTexEnviv(GLenum target, GLenum pname, const GLint *params);
void glTexEnvx(GLenum target, GLenum pname, GLfixed param);
void glTexEnvxv(GLenum target, GLenum pname, const GLfixed *params);
void glTexGenf(GLenum coord, GLenum pname, GLfloat param);
void glTexGenfv(GLenum coord, GLenum pname, const GLfloat *params);
void glTexGeni(GLenum coord, GLenum pname, GLint param);
void glTexGeniv(GLenum coord, GLenum pname, const GLint *params);
void glTexGenx(GLenum coord, GLenum pname, GLfixed param);
void glTexGenxv(GLenum coord, GLenum pname, const GLfixed *params);
void glTexParameterx(GLenum target, GLenum pname, GLfixed param);
void glTexParameterxv(GLenum target, GLenum pname, const GLfixed *params);
void glTranslatef(GLfloat x, GLfloat y, GLfloat z);
void glTranslatex(GLfixed x, GLfixed y, GLfixed z);
void glVertexPointer(GLint size, GLenum type, GLsizei stride, const void *pointer);

/* ------------------------------------------------------------------
 * Core GLES3 / desktop GL functions that are not in GLES2 headers
 * but are referenced by the GameEngine's import table.
 * These wrap the OES/EXT equivalents where possible.
 * ------------------------------------------------------------------ */
void glBindVertexArray(GLuint array);
void glDeleteVertexArrays(GLsizei n, const GLuint *arrays);
void glGenVertexArrays(GLsizei n, GLuint *arrays);
const GLubyte *glGetStringi(GLenum name, GLuint index);
void glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount);
void glDrawElementsInstancedEXT(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount);
void *glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access);
void *glMapBuffer(GLenum target, GLenum access);
GLboolean glUnmapBuffer(GLenum target);
void glVertexAttribDivisor(GLuint index, GLuint divisor);
void glVertexAttribDivisorEXT(GLuint index, GLuint divisor);

/* GLES1 fixed-point variants not in GLES2 */
void glLineWidthx(GLfixed width);
void glNormal3x(GLfixed nx, GLfixed ny, GLfixed nz);
void glPointSizex(GLfixed size);
void glPolygonOffsetx(GLfixed factor, GLfixed units);

/* ------------------------------------------------------------------
 * OES / EXT stubs
 * ------------------------------------------------------------------ */
void glCurrentPaletteMatrixOES(GLuint index);
void glDrawTexfOES(GLfloat x, GLfloat y, GLfloat z, GLfloat width, GLfloat height);
void glDrawTexfvOES(const GLfloat *coords);
void glDrawTexiOES(GLint x, GLint y, GLint z, GLint width, GLint height);
void glDrawTexivOES(const GLint *coords);
void glDrawTexsOES(GLshort x, GLshort y, GLshort z, GLshort width, GLshort height);
void glDrawTexsvOES(const GLshort *coords);
void glDrawTexxOES(GLfixed x, GLfixed y, GLfixed z, GLfixed width, GLfixed height);
void glDrawTexxvOES(const GLfixed *coords);
void glEGLImageTargetRenderbufferStorageOES(GLenum target, GLeglImageOES image);
void glEGLImageTargetTexture2DOES(GLenum target, GLeglImageOES image);
void glGetBufferPointervOES(GLenum target, GLenum pname, void **params);
void glLoadPaletteFromModelViewMatrixOES(void);
void glPointSizePointerOES(GLenum type, GLsizei stride, const void *pointer);
void glWeightPointerOES(GLint size, GLenum type, GLsizei stride, const void *pointer);

/* Android native-window stubs (needed by dynlib default symbol table) */
void *ANativeWindow_fromSurface(void *env, void *surface);
void ANativeWindow_release(void *window);

#ifdef __cplusplus
}
#endif

#endif /* USE_PVR_PSP2 */
#endif /* SOLOADER_PVR_GLES_STUBS_H */
