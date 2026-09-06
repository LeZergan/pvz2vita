/*
 * FalsoJNI.h
 *
 * Fake Java Native Interface, providing JavaVM and JNIEnv objects.
 *
 * Copyright (C) 2022 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef FALSOJNI_H
#define FALSOJNI_H

#define FALSOJNI_DEBUG_NO    4
#define FALSOJNI_DEBUG_ERROR 3
#define FALSOJNI_DEBUG_WARN  2
#define FALSOJNI_DEBUG_INFO  1
#define FALSOJNI_DEBUG_ALL   0

#ifndef FALSOJNI_DEBUGLEVEL
#define FALSOJNI_DEBUGLEVEL FALSOJNI_DEBUG_WARN
#endif

#include "jni.h"

#ifdef FALSOJNI_IMPLEMENTATION_SAMPLE
#include "FalsoJNI_ImplSample.h"
#endif

extern JavaVM jvm;
extern JNIEnv jni;

void jni_init();

/* Drain captured Nimble async callbacks: fire their native completions with an
 * empty result so offline async ops (MTX catalog refresh, ...) don't hang boot.
 * Call once per frame from the render loop (a non-reentrant point). */
void fjni_drain_nimble_cbs(void);

/* Drain captured Glu-SDK HTTP requests: fire onHTTPResponse with a network-error
 * status so offline sendHTTPRequest calls complete instead of hanging the loader
 * thread. Call once per frame from the render loop (a non-reentrant point). */
void fjni_drain_glu_http(void);
void fjni_drain_http_ok(void);   /* online spoof: deferred HTTP-transaction success */

/* Look up a native captured from RegisterNatives, by name (NULL if absent).
 * Lets the loader call PvZ2's engine entry points directly. */
void *falso_jni_get_native(const char *name);

/* Synthetic method IDs returned by Get(Static)MethodID for methods NOT in the
 * explicit table. Non-zero so the engine (which fail-fast bails on a 0 method
 * ID) proceeds. Differentiated by RETURN TYPE so the call dispatch returns the
 * right kind of safe default — crucially, NULL for non-String object methods
 * (e.g. GetActivity()Landroid/app/Activity;) so the engine's null-checks skip
 * them, rather than an empty String it would wrongly treat as a valid object
 * and crash dereferencing. Real IDs are ~1000-1100 so these can't collide. */
#define FJNI_STUB_METHOD_STR  ((jmethodID)0x7FFFFFF1)  /* String  -> ""   */
#define FJNI_STUB_METHOD_OBJ  ((jmethodID)0x7FFFFFF2)  /* object  -> NULL */
#define FJNI_STUB_METHOD_ID   ((jmethodID)0x7FFFFFF3)  /* primitive/void  */

/* Pick the stub ID for an unresolved method from its JNI signature. */
static inline jmethodID fjni_stub_for_sig(const char *sig) {
    const char *r = sig ? __builtin_strchr(sig, ')') : 0;
    if (r) r++;
    if (r && (*r == 'L' || *r == '[')) {
        if (*r == 'L' && __builtin_strcmp(r, "Ljava/lang/String;") == 0)
            return FJNI_STUB_METHOD_STR;
        return FJNI_STUB_METHOD_OBJ;
    }
    return FJNI_STUB_METHOD_ID;
}

#endif // FALSOJNI_H
