/*
 * FalsoJNI.c
 *
 * Fake Java Native Interface, providing JavaVM and JNIEnv objects.
 *
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 *
 * Contains parts of Dalvik implementation of JNI interfaces,
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 */

#pragma ide diagnostic ignored "UnusedParameter"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdatomic.h>

#include "FalsoJNI_ImplBridge.h"
#include "FalsoJNI_Logger.h"
#include "FalsoJNI.h"
#include "converter.h"
#include "jni_string_codec.h"

// Objects to be passed to client applications:
JavaVM jvm;
JNIEnv jni;

// Private structs backing the above objects:
struct JNIInvokeInterface * _jvm; // NOLINT(bugprone-reserved-identifier)
struct JNINativeInterface * _jni; // NOLINT(bugprone-reserved-identifier)

typedef enum TrackedObjKind {
    TRACKED_OBJ_UNKNOWN = 0,
    TRACKED_OBJ_CLASS = 1,
    TRACKED_OBJ_ARRAY = 2,
    TRACKED_OBJ_STRING = 3,
} TrackedObjKind;

typedef struct TrackedRef {
    jobject obj;
    TrackedObjKind kind;
    uint32_t local_refs;
    uint32_t global_refs;
    uint8_t seen_global;
    struct TrackedRef *next;
} TrackedRef;

/* Do not scan all objects ever created for each new JNI allocation. */
#define TRACKED_REF_BUCKETS 1024u
static TrackedRef *g_tracked_refs[TRACKED_REF_BUCKETS];
static uint32_t g_tracked_live, g_tracked_created, g_tracked_freed;
static unsigned tracked_bucket(jobject obj) {
    uintptr_t key = (uintptr_t)obj >> 3;
    key ^= key >> 11;
    return (unsigned)key & (TRACKED_REF_BUCKETS - 1u);
}
static atomic_flag g_tracked_refs_lock = ATOMIC_FLAG_INIT;

/* PvZ2Native interns class handles and gives every Java construction a
 * distinct, class-tagged fake object.  FalsoJNI's historical constant handles
 * collapse unrelated singletons into one object and make GetObjectClass lie,
 * which changes engine-side Java bridge decisions.  Keep the same lightweight
 * identity model here; these process-lifetime records are tiny compared with
 * libPVZ2 and deliberately never move or expire. */
typedef struct InternedClass {
    char *name;
    struct InternedClass *next;
} InternedClass;

typedef struct FakeJavaObject {
    uint32_t magic;
    jclass clazz;
    struct FakeJavaObject *next;
} FakeJavaObject;

#define FAKE_JAVA_OBJECT_MAGIC UINT32_C(0x464a4f42)

static InternedClass *g_interned_classes = NULL;
static FakeJavaObject *g_fake_java_objects = NULL;

static void tracked_refs_lock(void) {
    while (atomic_flag_test_and_set_explicit(&g_tracked_refs_lock, memory_order_acquire)) {
    }
}

static void tracked_refs_unlock(void) {
    atomic_flag_clear_explicit(&g_tracked_refs_lock, memory_order_release);
}

static jclass intern_class_name(const char *name) {
    if (!name) return NULL;

    tracked_refs_lock();
    for (InternedClass *entry = g_interned_classes; entry; entry = entry->next) {
        if (strcmp(entry->name, name) == 0) {
            jclass result = (jclass)entry->name;
            tracked_refs_unlock();
            return result;
        }
    }
    tracked_refs_unlock();

    InternedClass *created = (InternedClass *)malloc(sizeof(*created));
    char *copy = strdup(name);
    if (!created || !copy) {
        free(created);
        free(copy);
        return NULL;
    }
    created->name = copy;

    tracked_refs_lock();
    /* Recheck after allocating because constructors can run on worker threads. */
    for (InternedClass *entry = g_interned_classes; entry; entry = entry->next) {
        if (strcmp(entry->name, name) == 0) {
            jclass result = (jclass)entry->name;
            tracked_refs_unlock();
            free(copy);
            free(created);
            return result;
        }
    }
    created->next = g_interned_classes;
    g_interned_classes = created;
    tracked_refs_unlock();
    return (jclass)created->name;
}

static jobject new_fake_java_object(jclass clazz) {
    FakeJavaObject *created = (FakeJavaObject *)malloc(sizeof(*created));
    if (!created) return (jobject)0x69696969u;
    created->magic = FAKE_JAVA_OBJECT_MAGIC;
    created->clazz = clazz;
    tracked_refs_lock();
    created->next = g_fake_java_objects;
    g_fake_java_objects = created;
    tracked_refs_unlock();
    return (jobject)created;
}

static jclass fake_java_object_class(jobject obj) {
    jclass result = NULL;
    tracked_refs_lock();
    for (FakeJavaObject *entry = g_fake_java_objects; entry; entry = entry->next) {
        if ((jobject)entry == obj && entry->magic == FAKE_JAVA_OBJECT_MAGIC) {
            result = entry->clazz;
            break;
        }
    }
    tracked_refs_unlock();
    return result;
}

static int is_reserved_fake_ref(jobject obj) {
    const uintptr_t value = (uintptr_t)obj;
    return (value == 0u) || (value == 0x42424242u) || (value == 0x69696969u);
}

static TrackedRef *tracked_find_locked(jobject obj) {
    TrackedRef *entry = g_tracked_refs[tracked_bucket(obj)];
    while (entry) {
        if (entry->obj == obj) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

static int tracked_release_ref_if_known(jobject obj, int prefer_global);

static void tracked_free_object(jobject obj, TrackedObjKind kind) {
    if (!obj) {
        return;
    }

    switch (kind) {
        case TRACKED_OBJ_ARRAY: {
            JavaDynArray *array = (JavaDynArray *)obj;
            if (array->type == FIELD_TYPE_OBJECT) {
                jobject *elements = array->array;
                for (jsize i = 0; i < array->len; ++i)
                    tracked_release_ref_if_known(elements[i], 1);
            }
            (void)jda_free(array);
            break;
        }
        case TRACKED_OBJ_STRING: {
            JavaString *str = (JavaString *)obj;
            if (str->utf8) {
                (void)jda_free(str->utf8);
            }
            if (str->utf16) {
                (void)jda_free(str->utf16);
            }
            free(str);
            break;
        }
        case TRACKED_OBJ_CLASS:
            free(obj);
            break;
        case TRACKED_OBJ_UNKNOWN:
        default:
            break;
    }
}

static jobject tracked_register_local_owned(jobject obj, TrackedObjKind kind) {
    TrackedRef *existing;

    if (!obj || is_reserved_fake_ref(obj) || kind == TRACKED_OBJ_UNKNOWN) {
        return NULL;
    }

    tracked_refs_lock();
    existing = tracked_find_locked(obj);
    if (existing) {
        if (existing->kind == TRACKED_OBJ_UNKNOWN) {
            existing->kind = kind;
        }
        tracked_refs_unlock();
        return obj;
    }
    tracked_refs_unlock();

    TrackedRef *entry = (TrackedRef *)malloc(sizeof(TrackedRef));
    if (!entry) {
        fjni_logv_err("[JNI] failed to track local ref 0x%x", (int)obj);
        tracked_free_object(obj, kind);
        return NULL;
    }

    entry->obj = obj;
    entry->kind = kind;
    entry->local_refs = 1;
    entry->global_refs = 0;
    entry->seen_global = 0;
    entry->next = NULL;

    tracked_refs_lock();
    existing = tracked_find_locked(obj);
    if (existing) {
        if (existing->kind == TRACKED_OBJ_UNKNOWN) {
            existing->kind = kind;
        }
        tracked_refs_unlock();
        free(entry);
        return obj;
    }

    unsigned bucket = tracked_bucket(obj);
    entry->next = g_tracked_refs[bucket];
    g_tracked_refs[bucket] = entry;
    ++g_tracked_live;
    ++g_tracked_created;
    tracked_refs_unlock();
    return obj;
}

static void tracked_add_ref_if_known(jobject obj, int as_global) {
    if (!obj || is_reserved_fake_ref(obj)) {
        return;
    }

    tracked_refs_lock();
    TrackedRef *entry = tracked_find_locked(obj);
    if (entry) {
        if (as_global) entry->global_refs++;
        else entry->local_refs++;
    }
    tracked_refs_unlock();
}

static int tracked_release_ref_if_known(jobject obj, int prefer_global) {
    TrackedRef *released = NULL;
    int found = 0;

    if (!obj || is_reserved_fake_ref(obj)) {
        return 0;
    }

    tracked_refs_lock();

    TrackedRef **pp = &g_tracked_refs[tracked_bucket(obj)];
    while (*pp) {
        TrackedRef *entry = *pp;
        if (entry->obj != obj) {
            pp = &entry->next;
            continue;
        }

        found = 1;
        if (prefer_global && entry->global_refs > 0) {
            entry->global_refs--;
        } else if (!prefer_global && entry->local_refs > 0) {
            entry->local_refs--;
        }

        if (entry->local_refs == 0 && entry->global_refs == 0) {
            *pp = entry->next;
            released = entry;
            --g_tracked_live;
            ++g_tracked_freed;
        }
        break;
    }

    tracked_refs_unlock();

    if (released) {
        tracked_free_object(released->obj, released->kind);
        free(released);
    }

    return found;
}

void fjni_ref_stats(uint32_t *live, uint32_t *created, uint32_t *freed) {
    tracked_refs_lock();
    *live = g_tracked_live;
    *created = g_tracked_created;
    *freed = g_tracked_freed;
    tracked_refs_unlock();
}

/*
 * JavaVM Methods
 */

jint DestroyJavaVM(JavaVM* vm) {
    fjni_log_dbg("[JVM] DestroyJavaVM(): ignored");
    return 0;
}

jint AttachCurrentThread(JavaVM* vm, JNIEnv** p_env, void* thr_args) {
    fjni_log_dbg("[JVM] AttachCurrentThread(vm, *p_env, thr_args)");
    *p_env = &jni;
    return 0;
}

jint DetachCurrentThread(JavaVM* vm) {
    fjni_log_dbg("[JVM] DetachCurrentThread(): ignored");
    // Since we don't operate the actual Java VM, we don't need to care about
    // it being available only in one thread. Anyway, due to this restriction,
    // client applications are guaranteed to use JNI in a safe way.
    return 0;
}

jint GetEnv(JavaVM* vm, void** env, jint version) {
    fjni_logv_dbg("[JVM] GetEnv(vm, **env, version:%i)", version);
    if (!env) {
        fjni_logv_err("[JVM] GetEnv(vm, **env, version:%i): env is NULL!", version);
        return JNI_EINVAL;
    }
    *env = &jni;
    return JNI_OK;
}

jint AttachCurrentThreadAsDaemon(JavaVM* vm, JNIEnv** penv, void* thr_args) {
    fjni_log_dbg("[JVM] AttachCurrentThreadAsDaemon(vm, *p_env, thr_args)");
    if (!penv) {
        fjni_log_err("[JVM] AttachCurrentThreadAsDaemon(vm, *p_env, thr_args): p_env is NULL!");
        return JNI_EINVAL;
    }
    *penv = &jni;
    return JNI_OK;
}

/*
 * JNIEnv Methods
 */

jint GetVersion(JNIEnv * env) {
    fjni_log_dbg("[JNI] GetVersion(env)");
    return JNI_VERSION_1_6;
}

jclass DefineClass(JNIEnv* env, const char* name, jobject loader, const jbyte* buf, jsize bufLen) {
    // Supposed to load Java class from `.class`-containing `buf`. Can not be implemented.
    fjni_logv_warn("[JNI] DefineClass(env, \"%s\", 0x%x, 0x%x, %i): not implemented", name, loader, buf, bufLen);
    return NULL;
}

jclass FindClass(JNIEnv* env, const char* name) {
    // While we don't manipulate any actual classes here, the following code
    // serves two purposes:
    //   1) Being able to uniquely identify constructor methods for classes
    //      ("<init>"), since in GetMethodID we only receive class pointer.
    //   2) Providing a valid pointer to a valid object so that it behaves
    //      normally in memory and can be freed.

    jclass clazz = intern_class_name(name);
    fjni_logv_dbg("[JNI] FindClass(%s): 0x%x", name, (int)clazz);
    return clazz;
}

jclass GetSuperclass(JNIEnv* env, jclass clazz) {
    fjni_logv_dbg("[JNI] GetSuperclass(env, 0x%x): generic java/lang/Object", (int)clazz);
    return intern_class_name("java/lang/Object");
}

jboolean IsAssignableFrom(JNIEnv* env, jclass clazz1, jclass clazz2) {
    // Supposed to determine whether an object of clazz1 can be safely cast to clazz2.
    // We can not check that, so return JNI_TRUE always.
    fjni_logv_warn("[JNI] IsAssignableFrom(env, 0x%x, 0x%x): not implemented", (int)clazz1, (int)clazz2);
    return JNI_TRUE;
}

jmethodID FromReflectedMethod(JNIEnv* env, jobject method) {
    fjni_logv_warn("[JNI] FromReflectedMethod(env, 0x%x): not implemented", (int)method);
    return NULL;
}

jfieldID FromReflectedField(JNIEnv* env, jobject field) {
    fjni_logv_warn("[JNI] FromReflectedField(env, 0x%x): not implemented", (int)field);
    return NULL;
}

jobject ToReflectedMethod(JNIEnv* env, jclass cls, jmethodID methodID, jboolean isStatic) {
    fjni_logv_warn("[JNI] ToReflectedMethod(env, 0x%x, %i, %i): not implemented", (int)cls, methodID, isStatic);
    return NULL;
}

jobject ToReflectedField(JNIEnv* env, jclass cls, jfieldID fieldID, jboolean isStatic) {
    fjni_logv_warn("[JNI] ToReflectedField(env, 0x%x, %i, %i): not implemented", (int)cls, fieldID, isStatic);
    return 0;
}

// TODO: Handle exceptions

jint Throw(JNIEnv* env, jthrowable obj) {
    fjni_logv_warn("[JNI] Throw(env, 0x%x): not implemented", (int)obj);
    return 0;
}

jint ThrowNew(JNIEnv* env, jclass clazz, const char* message) {
    fjni_logv_warn("[JNI] ThrowNew(env, 0x%x, \"%s\"): not implemented", (int)clazz, message);
    return 0;
}

jthrowable ExceptionOccurred(JNIEnv* env) {
    fjni_log_dbg("[JNI] ExceptionOccurred(env): ignored");
    // We never have exceptions.
    return NULL;
}

void ExceptionDescribe(JNIEnv* env) {
    fjni_log_dbg("[JNI] ExceptionDescribe(env): ignored");
    // We never have exceptions.
}

void ExceptionClear(JNIEnv* env) {
    fjni_log_dbg("[JNI] ExceptionClear(env): ignored");
    // We never have exceptions.
}

void FatalError(JNIEnv* env, const char* msg) {
    fjni_log_err(msg);
    abort();
}

jint PushLocalFrame(JNIEnv* env, jint capacity) {
    fjni_logv_dbg("[JNI] PushLocalFrame(env, %i): ignored", capacity);
    // Since we operate with global pointers everywhere, no need to scope them.
    return 0;
}

jobject PopLocalFrame(JNIEnv* env, jobject result) {
    fjni_logv_dbg("[JNI] PopLocalFrame(env, 0x%x): ignored", (int)result);
    // Since we don't model frames, pass the result through unchanged.
    return result;
}

jobject NewGlobalRef(JNIEnv* env, jobject obj) {
    fjni_logv_dbg("[JNI] NewGlobalRef(env, 0x%x)", (int)obj);

    // The concept of global/local references really makes sense only with
    // a real JVM. Here, since we basically operate with shared global pointers
    // everywhere, it should be safe to just return `obj` back.
    // Keep reference counts for tracked owned objects so local/global lifetimes
    // remain compatible with JNI expectations.
    tracked_add_ref_if_known(obj, 1);

    return obj;
}

void DeleteGlobalRef(JNIEnv* env, jobject obj) {
    fjni_logv_dbg("[JNI] DeleteGlobalRef(env, 0x%x)", (int)obj);
    if (!tracked_release_ref_if_known(obj, 1) && !is_reserved_fake_ref(obj)) {
        fjni_logv_dbg("[JNI] DeleteGlobalRef(env, 0x%x): untracked ref ignored", (int)obj);
    }
}

void DeleteLocalRef(JNIEnv* env, jobject obj) {
    tracked_release_ref_if_known(obj, 0);
}

jboolean IsSameObject(JNIEnv* env, jobject ref1, jobject ref2) {
    fjni_logv_dbg("[JNI] IsSameObject(env, 0x%x, 0x%x)", (int)ref1, (int)ref2);
    return (ref1 == ref2) ? JNI_TRUE : JNI_FALSE;
}

jobject NewLocalRef(JNIEnv* env, jobject obj) {
    tracked_add_ref_if_known(obj, 0);
    return obj;
}

jint EnsureLocalCapacity(JNIEnv* env, jint capacity) {
    fjni_logv_dbg("[JNI] EnsureLocalCapacity(env, %i)", capacity);
    return JNI_OK;
}

/* Offline HTTP bypass. At boot the engine issues HTTP requests (remote config /
 * CDN / EA services) via a Java AndroidHttpTransaction, then polls a callback
 * that a Java network thread would fire on completion. Offline there is no Java
 * network layer, so the request never completes: the engine busy-polls and then
 * abort()s. We give the transaction constructor and its Start() distinct IDs.
 * The ctor's first (jlong) arg is the C++ transaction handle — we stash it and
 * return it AS the fake jobject, so when Start() is called on that same object
 * we can fire the native HttpTransactionError(handle) ourselves and let the
 * engine take its offline/failed path instead of hanging. */
#define MID_HTTP_INIT  ((jmethodID)0x7D000010)
#define MID_HTTP_START ((jmethodID)0x7D000011)
jlong g_http_last_native = 0;  /* handle of the most recently constructed txn */

/* ★ v1321: per-handle URL table for AndroidHttpTransaction, so the success drain can
 * serve the RIGHT canned response body per endpoint (the auth/validate SUCCESS is the
 * one gate that unblocks LogoScreen -> MainMenu; see deep first-run capture). */
#define HTTP_URL_QMAX 48
static struct { jlong h; char url[160]; } g_http_urls[HTTP_URL_QMAX];
static int g_http_url_n = 0;
static void fjni_http_set_url(jlong h, const char* url) {
    int i = g_http_url_n % HTTP_URL_QMAX; g_http_url_n++;
    g_http_urls[i].h = h;
    g_http_urls[i].url[0] = 0;
    if (url) { size_t k = 0; for (; url[k] && k < sizeof(g_http_urls[i].url)-1; k++) g_http_urls[i].url[k] = url[k]; g_http_urls[i].url[k] = 0; }
}
static const char* fjni_http_get_url(jlong h) {
    for (int i = 0; i < HTTP_URL_QMAX; i++) if (g_http_urls[i].h == h && g_http_urls[i].url[0]) return g_http_urls[i].url;
    return NULL;
}
static int fjni_str_contains(const char* s, const char* sub) {
    if (!s || !sub) return 0;
    for (const char* p = s; *p; p++) { const char* a = p; const char* b = sub;
        while (*a && *b && *a == *b) { a++; b++; } if (!*b) return 1; }
    return 0;
}

/* Nimble async-callback completion (offline). The game registers a native
 * completion (in the Nimble callback registry keyed by an int id), then
 * constructs `new com.ea.nimble.bridge.BaseNativeCallback(id)`, passes it to an
 * async op (e.g. MTX refreshAvailableCatalogItems), and WAITS idle for the op to
 * invoke Java_..._nativeCallback(id, result). Offline the op never completes, so
 * boot hangs at the loading screen (final gate). Capture the id on construction
 * and DRAIN it from the frame loop (non-reentrant) by calling the native
 * nativeCallback ourselves with an EMPTY result array = "op done, no items". The
 * native registry self-guards: an already-handled id logs "no matching ID" (no-op),
 * a pending id gets completed. */
#define MID_NIMBLE_CB    ((jmethodID)0x7D000030)  /* BaseNativeCallback(id) ctor */
#define MID_MTX_REFRESH  ((jmethodID)0x7D000031)  /* INimbleMTX.refreshAvailableCatalogItems() */
#define NIMBLE_CB_QMAX 64
static jint g_nimble_cb_q[NIMBLE_CB_QMAX];
static volatile int g_nimble_cb_head = 0, g_nimble_cb_tail = 0;
/* Remember the MOST-RECENT BaseNativeCallback id (the async op's completion handle).
 * The game does: getComponent(MTX) -> new BaseNativeCallback(id) -> mtx.refreshAvailable
 * CatalogItems(). We fire ONLY the id captured right before that refresh call = the MTX
 * catalog callback (the gate). Firing the EARLIER online-init callbacks crashed their
 * completions (data abort), so we never touch those. */
static volatile jint g_nimble_last_id = 0;
static volatile int  g_nimble_last_valid = 0;
/* Fire EVERY captured callback (with a valid result — proven not to crash on the MTX
 * one). The boot waits on SEVERAL async ops (MTX catalog, IAP, account/EVS...), each a
 * BaseNativeCallback; completing them all at once clears the chain of gates instead of
 * one per build. The wrapper (FUN_02103198) is safe with a non-empty result, and the
 * per-op user callbacks get an empty ["", "{}"] payload = "no data / done". */
static void nimble_cb_capture(jint id) { g_nimble_last_id = id; g_nimble_last_valid = 1; }
static void nimble_cb_enqueue_last(void) {   /* fire ONLY the MTX callback (firing all crashed) */
    if (!g_nimble_last_valid) return;
    int n = (g_nimble_cb_head + 1) % NIMBLE_CB_QMAX;
    if (n != g_nimble_cb_tail) { g_nimble_cb_q[g_nimble_cb_head] = g_nimble_last_id; g_nimble_cb_head = n; }
}
void fjni_drain_nimble_cbs(void) {
    extern uintptr_t g_pvz2_text_base_for_diag;   /* set by main.c apply_pvz2_hooks */
    if (!g_pvz2_text_base_for_diag) return;
    void (*nativeCallback)(void *, jobject, jint, jobject) =
        (void (*)(void *, jobject, jint, jobject))(g_pvz2_text_base_for_diag + 0x20ed64c + 1); /* thumb */
    int fired = 0;
    while (fired < 4 && g_nimble_cb_tail != g_nimble_cb_head) {
        jint id = g_nimble_cb_q[g_nimble_cb_tail];
        g_nimble_cb_tail = (g_nimble_cb_tail + 1) % NIMBLE_CB_QMAX;
        fired++;
        /* The completion (FUN_02103198) derefs result[0] (a status jstring, empty=ok)
         * and result[1] (catalog JSON, parsed by FUN_0210330c). An EMPTY result made
         * the vector begin-ptr NULL -> data abort at +0x1c. Provide a valid 2-element
         * result: ["" , "{}"] = no error, empty catalog. */
        jobjectArray res = jni->NewObjectArray(&jni, 2, NULL, NULL);
        jni->SetObjectArrayElement(&jni, res, 0, jni->NewStringUTF(&jni, ""));
        jni->SetObjectArrayElement(&jni, res, 1, jni->NewStringUTF(&jni, "{}"));
        fjni_logv_err("[NIMBLECB] firing MTX catalog completion id=%d (result=['','{}'])", (int)id);
        nativeCallback(&jni, (jobject)0x69696969u, id, res);
    }
}

jobject AllocObject(JNIEnv* env, jclass clazz) {
    fjni_logv_dbg("[JNI] AllocObject(env, 0x%x): allocating class-tagged fake object", (int)clazz);
    return new_fake_java_object(clazz);
}

jobject NewObject(JNIEnv* env, jclass clazz, jmethodID methodID, ...) {
    fjni_logv_dbg("[JNI] NewObject(env, 0x%x, %i)", clazz, methodID);

    if (methodID == MID_HTTP_INIT) {
        va_list a; va_start(a, methodID);
        jlong np = va_arg(a, jlong);
        va_end(a);
        g_http_last_native = np;
        fjni_logv_err("[HTTP] offline: transaction created native=0x%llx", (unsigned long long)np);
        return (jobject)(uintptr_t)np;
    }
    if (methodID == MID_NIMBLE_CB) {
        va_list a; va_start(a, methodID);
        jint id = va_arg(a, jint);
        va_end(a);
        nimble_cb_capture(id);
        return new_fake_java_object(clazz);
    }

    jobject ret;
    va_list args;
    va_start(args, methodID);
    ret = methodObjectCall(methodID, args);
    va_end(args);

    /* A constructor must not return NULL — `new X()` always yields an object.
     * The engine stores these as platform singletons (AndroidNotification, the
     * Glu AndroidPlatform, ...) and its JavaMethod layer SKIPS every call when
     * the stored jobject is NULL ("no jobject to call"), which also means the
     * consent JNI calls never reach us and their completion callbacks never
     * fire (black-screen hang). Hand back an opaque reserved fake handle so the
     * engine has a valid object to call methods on (they route through our
     * stubs / consent bypass). */
    if (!ret) ret = new_fake_java_object(clazz);
    return ret;
}

jobject NewObjectV(JNIEnv* env, jclass clazz, jmethodID methodID, va_list args) {
    fjni_logv_dbg("[JNI] NewObjectV(env, 0x%x, %i)", clazz, methodID);
    if (methodID == MID_HTTP_INIT) {
        jlong np = va_arg(args, jlong);
        jobject method_o = va_arg(args, jobject); /* ctor (J, String method, String url) */
        jobject url_o = va_arg(args, jobject);
        const char* url = url_o ? jni->GetStringUTFChars(&jni, (jstring)url_o, NULL) : NULL;
        fjni_logv_err("[HTTPURL] txn native=0x%llx url=%s", (unsigned long long)np, url ? url : "(null)");
        fjni_http_set_url(np, url);
        if (url) jni->ReleaseStringUTFChars(&jni, (jstring)url_o, (char*)url);
        (void)method_o;
        g_http_last_native = np;
        return (jobject)(uintptr_t)np;
    }
    if (methodID == MID_NIMBLE_CB) {
        nimble_cb_capture(va_arg(args, jint));
        return new_fake_java_object(clazz);
    }
    jobject ret = methodObjectCall(methodID, args);
    return ret ? ret : new_fake_java_object(clazz);
}

jobject NewObjectA(JNIEnv* env, jclass clazz, jmethodID methodID, const jvalue *args) {
    fjni_logv_dbg("[JNI] NewObjectA(env, 0x%x, %i)", (int)clazz, methodID);
    if (methodID == MID_HTTP_INIT) {
        jlong np = args[0].j;
        g_http_last_native = np;
        return (jobject)(uintptr_t)np;
    }
    if (methodID == MID_NIMBLE_CB) {
        nimble_cb_capture(args[0].i);
        return new_fake_java_object(clazz);
    }
    jobject ret = methodObjectCall(methodID, _AtoV(0, args));
    return ret ? ret : new_fake_java_object(clazz);
}

jclass GetObjectClass(JNIEnv* env, jobject obj) {
    fjni_logv_dbg("[JNI] GetObjectClass(0x%x)", (int)obj);
    jclass clazz = fake_java_object_class(obj);
    return clazz ? clazz : intern_class_name("java/lang/Object");
}

jboolean IsInstanceOf(JNIEnv* env, jobject obj, jclass clazz) {
    fjni_logv_dbg("[JNI] IsInstanceOf(env, 0x%x, 0x%x): permissive fake runtime", (int)obj, (int)clazz);
    return JNI_TRUE;
}

/* ------------------------------------------------------------------ *
 *  GDPR/CMP consent bypass (offline)
 * ------------------------------------------------------------------ *
 * The engine's consent flow calls Java methods (requestCMPConsentInfo,
 * showCMPConsentForm, showConsent), each passing its C++ ConsentManager handle
 * as the first (jlong) arg, then WAITS for a native completion callback that
 * the Java side would fire. Offline there is no Java, so those callbacks never
 * fire and the game hangs on an invisible consent screen (black). We give these
 * methods distinct IDs and, when called, fire the matching native completion
 * callback ourselves (with the same self handle) so the flow completes and the
 * game proceeds. */
#define MID_CONSENT_REQUEST  ((jmethodID)0x7D000001)
#define MID_CONSENT_SHOWFORM ((jmethodID)0x7D000002)
#define MID_CONSENT_SHOW     ((jmethodID)0x7D000003)

static int fjni_try_consent(JNIEnv* env, jmethodID mid, va_list args) {
    if (mid != MID_CONSENT_REQUEST && mid != MID_CONSENT_SHOWFORM &&
        mid != MID_CONSENT_SHOW)
        return 0;
    jlong self = va_arg(args, jlong);  /* first arg of every consent method */
    if (mid == MID_CONSENT_REQUEST) {
        void (*cb)(JNIEnv*, jobject, jlong, jboolean) =
            (void*)falso_jni_get_native("onRequestCMPConsentInfoComplete");
        fjni_logv_err("[Consent] auto-completing requestCMPConsentInfo (self=0x%llx) cb=%p",
                      (unsigned long long)self, (void*)cb);
        if (cb) cb(env, NULL, self, JNI_TRUE);
    } else if (mid == MID_CONSENT_SHOWFORM) {
        void (*cb)(JNIEnv*, jobject, jlong, jboolean, jstring, jstring, jstring,
                   jstring, jstring) =
            (void*)falso_jni_get_native("onShowCMPConsentFormComplete");
        fjni_logv_err("[Consent] auto-completing showCMPConsentForm (self=0x%llx) cb=%p",
                      (unsigned long long)self, (void*)cb);
        if (cb) cb(env, NULL, self, JNI_TRUE, NULL, NULL, NULL, NULL, NULL);
    } else { /* MID_CONSENT_SHOW */
        void (*cb)(JNIEnv*, jobject, jlong, jstring) =
            (void*)falso_jni_get_native("onShowConsentComplete");
        fjni_logv_err("[Consent] auto-completing showConsent (self=0x%llx) cb=%p",
                      (unsigned long long)self, (void*)cb);
        if (cb) cb(env, NULL, self, NULL);
    }
    return 1;
}

/* Deferred HTTP-transaction SUCCESS queue (online spoof): Start() enqueues, the main-loop
 * drain fjni_drain_http_ok() fires HttpReceivedResponse -> HttpTransactionComplete. */
#define HTTP_OK_QMAX 16
static jlong g_http_ok_q[HTTP_OK_QMAX];
static volatile int g_http_ok_head = 0, g_http_ok_tail = 0;

/* Fire HttpTransactionError for the offline HTTP bypass. Only acts when Start()
 * is called on the exact object we created for the transaction (obj == the
 * stashed native handle), so an unrelated Start()V elsewhere is left alone. */
static int fjni_try_http(JNIEnv* env, jobject obj, jmethodID mid) {
    if (mid != MID_HTTP_START) return 0;
    jlong self = (jlong)(uintptr_t)obj;
    if (self == 0 || self != g_http_last_native) return 0;
    extern int g_pvz2_online_spoof, g_pvz2_http_txn_ok;
    if (g_pvz2_online_spoof && g_pvz2_http_txn_ok) {
        /* online spoof (opt-in enable_http_txn_ok.txt): DEFER the SUCCESS callbacks to the main
         * loop (non-reentrant). NOTE: on Vita3K the game then crashes proceeding (a NULL online-
         * only virtual method call — a partially-constructed object); the loader's NULL-guard
         * abort handler catches that on REAL VITA but Vita3K doesn't route prefetch aborts. */
        g_http_ok_q[g_http_ok_head] = self;
        g_http_ok_head = (g_http_ok_head + 1) % HTTP_OK_QMAX;
        fjni_logv_err("[HTTP] spoof: queued SUCCESS for Start (self=0x%llx)", (unsigned long long)self);
        return 1;
    }
    void (*cb)(JNIEnv*, jobject, jlong) =
        (void*)falso_jni_get_native("HttpTransactionError");
    fjni_logv_err("[HTTP] offline: auto-failing Start (self=0x%llx) cb=%p",
                  (unsigned long long)self, (void*)cb);
    if (cb) cb(env, NULL, self);
    return 1;
}

/* ---- Offline Glu-SDK HTTP bypass (build glu-http, 2026-07-07) ----------------
 * The glucentralservices SDK (Tags/EVS/Account) issues HTTP via
 * AndroidPlatform.sendHTTPRequest(url, method, headers[], body, JJ) and waits for
 * the registered native `onHTTPResponse(J handle, I status, [B body, Map headers)`
 * to fire. sendHTTPRequest was an unimplemented no-op -> the callback never fired
 * -> any Glu op the loader waits on (a config/tags/EVS fetch) hangs = the post-load
 * stall. Capture the request handle + LOG the url, then fire onHTTPResponse with a
 * network-error status from the main-loop drain (non-reentrant) so the SDK takes
 * its offline path. Mirrors fjni_try_http (AndroidHttpTransaction) + the Nimble
 * drain. First J of sendHTTPRequest == the J onHTTPResponse expects. */
#define MID_GLU_HTTP     ((jmethodID)0x7D000040)
#define MID_GLU_DOWNLOAD ((jmethodID)0x7D000041)
#define GLU_HTTP_QMAX 32
static jlong g_glu_http_q[GLU_HTTP_QMAX];
static volatile int g_glu_http_head = 0, g_glu_http_tail = 0;
/* Same pattern for downloadFile(url,dest,JJ,Z) -> onDownloadResponse(J,I,String):
 * the Glu SDK downloads CDN/patch content and waits on the completion native, which
 * was never fired (downloadFile stubbed to a no-op) = a post-SDK_CONFIG_PIN gate. */
static jlong g_glu_dl_q[GLU_HTTP_QMAX];
static char  g_glu_dl_path[GLU_HTTP_QMAX][256];
static volatile int g_glu_dl_head = 0, g_glu_dl_tail = 0;

static int fjni_try_glu_download(JNIEnv* env, jmethodID mid, va_list args) {
    (void)env;
    if (mid != MID_GLU_DOWNLOAD) return 0;
    jobject url_o  = va_arg(args, jobject);
    jobject dest_o = va_arg(args, jobject);
    (void)va_arg(args, jlong);       /* timeout_ms */
    jlong handle = va_arg(args, jlong);
    (void)va_arg(args, jint);        /* jboolean flag */
    const char* url  = url_o  ? jni->GetStringUTFChars(&jni, (jstring)url_o, NULL)  : NULL;
    const char* dest = dest_o ? jni->GetStringUTFChars(&jni, (jstring)dest_o, NULL) : NULL;
    fjni_logv_err("[GLUHTTP] downloadFile handle=0x%llx url=%s dest=%s",
                  (unsigned long long)handle, url ? url : "(null)", dest ? dest : "(null)");
    int n = (g_glu_dl_head + 1) % GLU_HTTP_QMAX;
    if (n != g_glu_dl_tail) {
        g_glu_dl_q[g_glu_dl_head] = handle;
        g_glu_dl_path[g_glu_dl_head][0] = 0;
        if (dest) { size_t k = 0; for (; dest[k] && k < 255; k++) g_glu_dl_path[g_glu_dl_head][k] = dest[k]; g_glu_dl_path[g_glu_dl_head][k] = 0; }
        g_glu_dl_head = n;
    }
    if (url)  jni->ReleaseStringUTFChars(&jni, (jstring)url_o,  (char*)url);
    if (dest) jni->ReleaseStringUTFChars(&jni, (jstring)dest_o, (char*)dest);
    return 1;   /* consume the no-op offline download */
}

static int fjni_try_glu_http(JNIEnv* env, jmethodID mid, va_list args) {
    (void)env;
    if (mid != MID_GLU_HTTP) return 0;
    jobject url_o    = va_arg(args, jobject);
    jobject method_o = va_arg(args, jobject);
    (void)va_arg(args, jobject);   /* String[] headers */
    (void)va_arg(args, jobject);   /* String body */
    jlong h1 = va_arg(args, jlong);
    jlong h2 = va_arg(args, jlong);
    const char* url = url_o ? jni->GetStringUTFChars(&jni, (jstring)url_o, NULL) : NULL;
    const char* method = method_o ? jni->GetStringUTFChars(&jni, (jstring)method_o, NULL) : NULL;
    fjni_logv_err("[GLUHTTP] sendHTTPRequest method=%s timeout=0x%llx handle=0x%llx url=%s",
                  method ? method : "(null)",
                  (unsigned long long)h1, (unsigned long long)h2, url ? url : "(null)");
    if (url) jni->ReleaseStringUTFChars(&jni, (jstring)url_o, (char*)url);
    if (method) jni->ReleaseStringUTFChars(&jni, (jstring)method_o, (char*)method);
    /* The handle onHTTPResponse expects is the SECOND J (a heap request-context
     * pointer, e.g. 0x84a540c0); the FIRST J is the timeout in ms (e.g. 0x2710 =
     * 10000). Firing with the first J data-aborts inside onHTTPResponse@+0x1b. */
    int n = (g_glu_http_head + 1) % GLU_HTTP_QMAX;
    if (n != g_glu_http_tail) { g_glu_http_q[g_glu_http_head] = h2; g_glu_http_head = n; }
    return 1;   /* consume the no-op offline request */
}

/* Fire the queued Glu HTTP completions from the main render loop (like
 * fjni_drain_nimble_cbs) — off the SDK's calling thread, non-reentrant. status 0 =
 * network error (offline); empty body; NULL headers map. */
extern int g_pvz2_online_spoof;   /* defined below; online-spoof master flag */
void fjni_drain_glu_http(void) {
    void (*cb)(JNIEnv*, jobject, jlong, jint, jbyteArray, jobject) =
        (void*)falso_jni_get_native("onHTTPResponse");
    int fired = 0;
    jint st = g_pvz2_online_spoof ? 200 : 0;   /* ★ online spoof: report HTTP success */
    while (cb && fired < 8 && g_glu_http_tail != g_glu_http_head) {
        jlong h = g_glu_http_q[g_glu_http_tail];
        g_glu_http_tail = (g_glu_http_tail + 1) % GLU_HTTP_QMAX;
        fired++;
        jbyteArray body;
        if (g_pvz2_online_spoof) {                 /* minimal valid-JSON body {} */
            const jbyte j2[2] = { (jbyte)'{', (jbyte)'}' };
            body = jni->NewByteArray(&jni, 2);
            if (body) jni->SetByteArrayRegion(&jni, body, 0, 2, j2);
        } else {
            body = jni->NewByteArray(&jni, 0);
        }
        fjni_logv_err("[GLUHTTP] firing onHTTPResponse(h=0x%llx status=%d)", (unsigned long long)h, (int)st);
        cb(&jni, (jobject)0x69696969u, h, st, body, NULL);
    }
    /* Drain the download queue -> onDownloadResponse(J handle, I status, String path). */
    void (*dcb)(JNIEnv*, jobject, jlong, jint, jstring) =
        (void*)falso_jni_get_native("onDownloadResponse");
    int dfired = 0;
    while (dcb && dfired < 8 && g_glu_dl_tail != g_glu_dl_head) {
        jlong h = g_glu_dl_q[g_glu_dl_tail];
        const char* path = g_glu_dl_path[g_glu_dl_tail];
        g_glu_dl_tail = (g_glu_dl_tail + 1) % GLU_HTTP_QMAX;
        dfired++;
        fjni_logv_err("[GLUHTTP] firing onDownloadResponse(h=0x%llx status=0 offline)", (unsigned long long)h);
        dcb(&jni, (jobject)0x69696969u, h, 0, jni->NewStringUTF(&jni, path));
    }
}

/* ★ Deferred HTTP-transaction SUCCESS drain (online spoof). Fires the framework HTTP
 * transaction's success callbacks from the main loop (non-reentrant): HttpReceivedResponse
 * (status read 200 via int-poll spoof) then HttpTransactionComplete. Empty body first (the
 * consumer may crash parsing a wrong-shaped body; empty lets it take its no-data path). */
void fjni_drain_http_ok(void) {
    void (*resp)(JNIEnv*, jobject, jlong) = (void*)falso_jni_get_native("HttpReceivedResponse");
    void (*data)(JNIEnv*, jobject, jlong, jbyteArray, jint) = (void*)falso_jni_get_native("HttpReceivedData");
    void (*comp)(JNIEnv*, jobject, jlong) = (void*)falso_jni_get_native("HttpTransactionComplete");
    int fired = 0;
    while (fired < 8 && g_http_ok_tail != g_http_ok_head) {
        jlong h = g_http_ok_q[g_http_ok_tail];
        g_http_ok_tail = (g_http_ok_tail + 1) % HTTP_OK_QMAX;
        fired++;
        const char* url = fjni_http_get_url(h);
        if (resp) resp(&jni, NULL, h);
        /* ★ v1321: the auth/validate handshake is THE gate to leave LogoScreen (deep first-run
         * capture). Serve the exact SUCCESS body echoing our pcpId; other endpoints get empty. */
        if (data && url && fjni_str_contains(url, "auth/v1/validate")) {
            const char* body = "{\"result\":\"SUCCESS\",\"expectedId\":\"A1B2C3D4-E5F6-4788-9ABC-DEF012345678\"}";
            int len = 0; while (body[len]) len++;
            jbyteArray arr = jni->NewByteArray(&jni, len);
            if (arr) jni->SetByteArrayRegion(&jni, arr, 0, len, (const jbyte*)body);
            fjni_logv_err("[HTTP] AUTH/VALIDATE -> serving SUCCESS body (self=0x%llx)", (unsigned long long)h);
            if (arr) data(&jni, NULL, h, arr, len);
        }
        fjni_logv_err("[HTTP] draining SUCCESS (self=0x%llx url=%s)", (unsigned long long)h, url ? url : "(null)");
        if (comp) comp(&jni, NULL, h);
    }
}

/* ★ ONLINE SPOOF master flag (van-gsm9): set from ux0:data/pvz2/enable_online_spoof.txt in
 * main(). When on, the whole online layer reports SUCCESS (authed, connected, HTTP 200) so the
 * game's boot online-init completes instead of stalling at GAME_LogoScreen (network=0 pushed it
 * onto a half-built offline branch that crashed; this keeps it on the online branch that fully
 * constructs its objects). */
int g_pvz2_online_spoof = 0;
int g_pvz2_http_txn_ok = 0;   /* opt-in: HTTP transactions report SUCCESS (crashes on Vita3K, may work on real Vita) */

jmethodID GetMethodID(JNIEnv* env, jclass clazz, const char* _name, const char* sig) {
    jmethodID ret;
    char name[512];

    if (strcmp("<init>", _name) == 0) {
        if (!clazz) {
            fjni_log_err("Cannot find constructor method ID for class NULL");
            return NULL;
        }

        // In FindClass we return a char ptr of class name as `clazz`, so we
        // can use it here for distinguishing different constructors
        snprintf(name, sizeof(name), "%s/%s", (char*)clazz, _name);
    } else {
        snprintf(name, sizeof(name), "%s", _name);
    }

    /* Nimble async callback ctor: capture the id so we can fire its native
     * completion offline (see fjni_drain_nimble_cbs). Both '/' and '.' name forms. */
    if (strcmp(name, "com/ea/nimble/bridge/BaseNativeCallback/<init>") == 0 ||
        strcmp(name, "com.ea.nimble.bridge.BaseNativeCallback/<init>") == 0)
        return MID_NIMBLE_CB;
    /* Boot async void-triggers: each kicks off an online op the boot waits on; fire the
     * callback captured just before it so the completion resolves offline. Deliberately
     * whitelisted (firing ALL callbacks crashed the early online-init ones). */
    if (strcmp(name, "refreshAvailableCatalogItems") == 0 ||
        strcmp(name, "restoreTransactions") == 0)
        return MID_MTX_REFRESH;

    /* Consent methods get distinct IDs so CallVoidMethod can fire their
     * completion callbacks (see fjni_try_consent). */
    if (strcmp(name, "requestCMPConsentInfo") == 0) return MID_CONSENT_REQUEST;
    if (strcmp(name, "showCMPConsentForm")    == 0) return MID_CONSENT_SHOWFORM;
    if (strcmp(name, "showConsent")           == 0) return MID_CONSENT_SHOW;

    /* Offline HTTP bypass: tag the AndroidHttpTransaction constructor and Start()
     * so NewObject can stash the native handle and CallVoidMethod can fail it. */
    if (strcmp(name, "com/popcap/SexyAppFramework/AndroidHttpTransaction/<init>") == 0)
        return MID_HTTP_INIT;
    if (strcmp(name, "Start") == 0 && sig && strcmp(sig, "()V") == 0)
        return MID_HTTP_START;

    /* Offline Glu-SDK HTTP: intercept sendHTTPRequest so we can complete it with a
     * network-error callback instead of hanging (see fjni_try_glu_http). */
    if (strcmp(name, "sendHTTPRequest") == 0)
        return MID_GLU_HTTP;
    if (strcmp(name, "downloadFile") == 0)
        return MID_GLU_DOWNLOAD;
    ret = getMethodIdByName(name);

    if (ret != NULL) {
        fjni_logv_dbg("[JNI] GetMethodID(env, 0x%x, \"%s\", \"%s\"): %i", (int)clazz, name, sig, (int)ret);
    } else {
        /* Not in the explicit table (config/device query, etc.). Return a stub
         * ID (typed by return signature) so the engine doesn't fail-fast bail;
         * the call returns a safe, correctly-TYPED default. Explicit methods
         * (obb name, width/height, ...) still win. */
        fjni_logv_warn("[JNI] GetMethodID(\"%s\", \"%s\"): not in table — stubbed", name, sig);
        const char* r = sig ? strchr(sig, ')') : NULL;
        char rt = (r && r[1]) ? r[1] : 'V';
        /* Name-track every unknown Z/I/V/object method so a polled/called method
         * is identifiable in the log (defaults unchanged: bool false, int 0,
         * String "", object NULL, void nothing). J/F/D/etc. keep the fixed stub. */
        if (rt == 'Z')      ret = fjni_register_namestub(name, 'Z');
        else if (rt == 'I') ret = fjni_register_namestub(name, 'I');
        else if (rt == 'V') ret = fjni_register_namestub(name, 'V');
        else if (rt == 'D') ret = fjni_register_namestub(name, 'D');
        else if (rt == 'F') ret = fjni_register_namestub(name, 'F');
        else if (rt == 'J') ret = fjni_register_namestub(name, 'J');
        else if (rt == 'L') ret = fjni_register_namestub(name,
                                    (sig && strstr(sig, ")Ljava/lang/String;")) ? 'S' : 'L');
        else ret = fjni_stub_for_sig(sig);
    }

    return ret;
}

jobject CallObjectMethod(JNIEnv* env, jobject obj, jmethodID methodID, ...) {
    fjni_logv_dbg("[JNI] CallObjectMethod(env, 0x%x, %i)", (int)obj, methodID);

    jobject ret;
    va_list args;
    va_start(args, methodID);
    ret = methodObjectCall(methodID, args);
    va_end(args);

    return ret;
}

jobject CallObjectMethodV(JNIEnv* env, jobject obj, jmethodID methodID, va_list args) {
    fjni_logv_dbg("[JNI] CallObjectMethodV(env, 0x%x, %i, args)", (int)obj, (int)methodID);
    return methodObjectCall(methodID, args);
}

jobject CallObjectMethodA(JNIEnv* env, jobject obj, jmethodID methodID, const jvalue* args) {
    fjni_logv_dbg("[JNI] CallObjectMethodA(env, 0x%x, %i, args)", (int)obj, (int)methodID);
    return methodObjectCall(methodID, _AtoV(0, args));
}

/* Return address of the GAME code that called the most recent CallBooleanMethod*
 * (depth-0 builtin = the direct caller = libPVZ2). ConfigKeyExists reads this to
 * identify the native site that polls "wctgc" every frame, for Ghidra decompile. */
void *g_fjni_bool_caller = NULL;

jboolean CallBooleanMethod(JNIEnv* env, jobject obj, jmethodID methodID, ...) {
    g_fjni_bool_caller = __builtin_return_address(0);
    fjni_logv_dbg("[JNI] CallBooleanMethod(env, 0x%x, %i)", (int)obj, methodID);

    jboolean ret;
    va_list args;
    va_start(args, methodID);
    ret = methodBooleanCall(methodID, args);
    va_end(args);

    return ret;
}

jboolean CallBooleanMethodV(JNIEnv* env, jobject obj, jmethodID methodID, va_list args) {
    g_fjni_bool_caller = __builtin_return_address(0);
    fjni_logv_dbg("[JNI] CallBooleanMethodV(env, 0x%x, %i, args)", (int)obj, (int)methodID);
    return methodBooleanCall(methodID, args);
}

jboolean CallBooleanMethodA(JNIEnv* env, jobject obj, jmethodID methodID, const jvalue* args) {
    g_fjni_bool_caller = __builtin_return_address(0);
    fjni_logv_dbg("[JNI] CallBooleanMethodA(env, 0x%x, %i, args)", (int)obj, (int)methodID);
    return methodBooleanCall(methodID, _AtoV(0, args));
}

jbyte CallByteMethod(JNIEnv* env, jobject obj, jmethodID methodID, ...) {
    fjni_logv_dbg("[JNI] CallByteMethod(env, 0x%x, %i)", (int)obj, methodID);

    jbyte ret;
    va_list args;
    va_start(args, methodID);
    ret = methodByteCall(methodID, args);
    va_end(args);

    return ret;
}

jbyte CallByteMethodV(JNIEnv* env, jobject obj, jmethodID methodID, va_list args) {
    fjni_logv_dbg("[JNI] CallByteMethodV(env, 0x%x, %i, args)", (int)obj, (int)methodID);
    return methodByteCall(methodID, args);
}

jbyte CallByteMethodA(JNIEnv* env, jobject obj, jmethodID methodID, const jvalue* args) {
    fjni_logv_dbg("[JNI] CallByteMethodA(env, 0x%x, %i, args)", (int)obj, (int)methodID);
    return methodByteCall(methodID, _AtoV(0, args));
}

jchar CallCharMethod(JNIEnv* env, jobject obj, jmethodID methodID, ...) {
    fjni_logv_dbg("[JNI] CallCharMethod(env, 0x%x, %i)", (int)obj, methodID);

    jchar ret;
    va_list args;
    va_start(args, methodID);
    ret = methodCharCall(methodID, args);
    va_end(args);

    return ret;
}

jchar CallCharMethodV(JNIEnv* env, jobject obj, jmethodID methodID, va_list args) {
    fjni_logv_dbg("[JNI] CallCharMethodV(env, 0x%x, %i, args)", (int)obj, (int)methodID);
    return methodCharCall(methodID, args);
}

jchar CallCharMethodA(JNIEnv* env, jobject obj, jmethodID methodID, const jvalue* args) {
    fjni_logv_dbg("[JNI] CallCharMethodA(env, 0x%x, %i, args)", (int)obj, (int)methodID);
    return methodCharCall(methodID, _AtoV(0, args));
}

jshort CallShortMethod(JNIEnv* env, jobject obj, jmethodID methodID, ...) {
    fjni_logv_dbg("[JNI] CallShortMethod(env, 0x%x, %i)", (int)obj, methodID);

    jshort ret;
    va_list args;
    va_start(args, methodID);
    ret = methodShortCall(methodID, args);
    va_end(args);

    return ret;
}

jshort CallShortMethodV(JNIEnv* env, jobject obj, jmethodID methodID, va_list args) {
    fjni_logv_dbg("[JNI] CallShortMethodV(env, 0x%x, %i, args)", (int)obj, (int)methodID);
    return methodShortCall(methodID, args);
}

jshort CallShortMethodA(JNIEnv* env, jobject obj, jmethodID methodID, const jvalue* args) {
    fjni_logv_dbg("[JNI] CallShortMethodA(env, 0x%x, %i, args)", (int)obj, (int)methodID);
    return methodShortCall(methodID, _AtoV(0, args));
}

jint CallIntMethod(JNIEnv* env, jobject obj, jmethodID methodID, ...) {
    fjni_logv_dbg("[JNI] CallIntMethod(env, 0x%x, %i, ...)", (int)obj, methodID);

    jint ret;
    va_list args;
    va_start(args, methodID);
    ret = methodIntCall(methodID, args);
    va_end(args);

    return ret;
}

jint CallIntMethodV(JNIEnv* env, jobject obj, jmethodID methodID, va_list args) {
    fjni_logv_dbg("[JNI] CallIntMethodV(env, 0x%x, %i, args)", (int)obj, methodID);
    return methodIntCall(methodID, args);
}

jint CallIntMethodA(JNIEnv* env, jobject obj, jmethodID methodID, const jvalue* args) {
    fjni_logv_dbg("[JNI] CallIntMethodA(env, 0x%x, %i, args)", (int)obj, methodID);
    return methodIntCall(methodID, _AtoV(0, args));
}

jlong CallLongMethod(JNIEnv* env, jobject obj, jmethodID methodID, ...) {
    fjni_logv_dbg("[JNI] CallLongMethod(env, 0x%x, %i, ...)", (int)obj, methodID);

    jlong ret;
    va_list args;
    va_start(args, methodID);
    ret = methodLongCall(methodID, args);
    va_end(args);

    return ret;
}

jlong CallLongMethodV(JNIEnv* env, jobject obj, jmethodID methodID, va_list args) {
    fjni_logv_dbg("[JNI] CallLongMethodV(env, 0x%x, %i, args)", (int)obj, methodID);
    return methodLongCall(methodID, args);
}

jlong CallLongMethodA(JNIEnv* env, jobject obj, jmethodID methodID, const jvalue* args) {
    fjni_logv_dbg("[JNI] CallLongMethodA(env, 0x%x, %i, args)", (int)obj, methodID);
    return methodLongCall(methodID, _AtoV(0, args));
}

jfloat CallFloatMethod(JNIEnv* env, jobject obj, jmethodID methodID, ...) {
    fjni_logv_dbg("[JNI] CallFloatMethod(env, 0x%x, %i, ...)", (int)obj, methodID);

    jfloat ret;
    va_list args;
    va_start(args, methodID);
    ret = methodFloatCall(methodID, args);
    va_end(args);

    return ret;
}

jfloat CallFloatMethodV(JNIEnv* env, jobject obj, jmethodID methodID, va_list args) {
    fjni_logv_dbg("[JNI] CallFloatMethodV(env, 0x%x, %i, args)", (int)obj, methodID);
    return methodFloatCall(methodID, args);
}

jfloat CallFloatMethodA(JNIEnv* env, jobject obj, jmethodID methodID, const jvalue* args) {
    fjni_logv_dbg("[JNI] CallFloatMethodA(env, 0x%x, %i, args)", (int)obj, methodID);
    return methodFloatCall(methodID, _AtoV(0, args));
}

jdouble CallDoubleMethod(JNIEnv* env, jobject obj, jmethodID methodID, ...) {
    fjni_logv_dbg("[JNI] CallDoubleMethod(env, 0x%x, %i, ...)", (int)obj, methodID);

    jdouble ret;
    va_list args;
    va_start(args, methodID);
    ret = methodDoubleCall(methodID, args);
    va_end(args);

    return ret;
}

jdouble CallDoubleMethodV(JNIEnv* env, jobject obj, jmethodID methodID, va_list args) {
    fjni_logv_dbg("[JNI] CallDoubleMethodV(env, 0x%x, %i, args)", (int)obj, methodID);
    return methodDoubleCall(methodID, args);
}

jdouble CallDoubleMethodA(JNIEnv* env, jobject obj, jmethodID methodID, const jvalue* args) {
    fjni_logv_dbg("[JNI] CallDoubleMethodA(env, 0x%x, %i, args)", (int)obj, methodID);
    return methodDoubleCall(methodID, _AtoV(0, args));
}

void CallVoidMethod(JNIEnv* env, jobject obj, jmethodID methodID, ...) {
    fjni_logv_dbg("[JNI] CallVoidMethod(env, 0x%x, %i, ...)", (int)obj, methodID);

    va_list args;
    va_start(args, methodID);
    if (fjni_try_http(env, obj, methodID)) { va_end(args); return; }
    if (fjni_try_glu_http(env, methodID, args)) { va_end(args); return; }
    if (fjni_try_glu_download(env, methodID, args)) { va_end(args); return; }
    if (fjni_try_consent(env, methodID, args)) { va_end(args); return; }
    if (methodID == MID_MTX_REFRESH) { nimble_cb_enqueue_last(); va_end(args); return; }
    methodVoidCall(methodID, args);
    va_end(args);
}

void CallVoidMethodV(JNIEnv* env, jobject obj, jmethodID methodID, va_list args) {
    fjni_logv_dbg("[JNI] CallVoidMethodV(env, 0x%x, %i, args)", (int)obj, methodID);
    if (fjni_try_http(env, obj, methodID)) return;
    if (fjni_try_glu_http(env, methodID, args)) return;
    if (fjni_try_glu_download(env, methodID, args)) return;
    if (fjni_try_consent(env, methodID, args)) return;
    if (methodID == MID_MTX_REFRESH) { nimble_cb_enqueue_last(); return; }
    methodVoidCall(methodID, args);
}

void CallVoidMethodA(JNIEnv* env, jobject obj, jmethodID methodID, const jvalue* args) {
    fjni_logv_dbg("[JNI] CallVoidMethodA(env, 0x%x, %i, args)", (int)obj, methodID);
    if (fjni_try_http(env, obj, methodID)) return;
    if (fjni_try_glu_http(env, methodID, _AtoV(0, args))) return;
    if (fjni_try_glu_download(env, methodID, _AtoV(0, args))) return;
    if (fjni_try_consent(env, methodID, _AtoV(0, args))) return;
    if (methodID == MID_MTX_REFRESH) { nimble_cb_enqueue_last(); return; }
    methodVoidCall(methodID, _AtoV(0, args));
}

jobject CallNonvirtualObjectMethod(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, ...) {
    fjni_logv_dbg("[JNI] CallNonvirtualObjectMethod(env, 0x%x, 0x%x, %i, ...)", (int)obj, (int)clazz, methodID);

    jobject ret;
    va_list args;
    va_start(args, methodID);
    ret = methodObjectCall(methodID, args);
    va_end(args);

    return ret;
}

jobject CallNonvirtualObjectMethodV(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, va_list args) {
    fjni_logv_dbg("[JNI] CallNonvirtualObjectMethodV(env, 0x%x, 0x%x, %i, args)", (int)obj, (int)clazz, methodID);
    return methodObjectCall(methodID, args);
}

jobject CallNonvirtualObjectMethodA(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, const jvalue* args) {
    fjni_logv_dbg("[JNI] CallNonvirtualObjectMethodA(env, 0x%x, 0x%x, %i, args)", (int)obj, (int)clazz, methodID);
    return methodObjectCall(methodID, _AtoV(0, args));
}

jboolean CallNonvirtualBooleanMethod(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, ...) {
    fjni_logv_dbg("[JNI] CallNonvirtualBooleanMethod(env, 0x%x, 0x%x, %i, ...)", (int)obj, (int)clazz, methodID);

    jboolean ret;
    va_list args;
    va_start(args, methodID);
    ret = methodBooleanCall(methodID, args);
    va_end(args);

    return ret;
}

jboolean CallNonvirtualBooleanMethodV(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, va_list args) {
    fjni_logv_dbg("[JNI] CallNonvirtualBooleanMethodV(env, 0x%x, 0x%x, %i, args)", (int)obj, (int)clazz, methodID);
    return methodBooleanCall(methodID, args);
}

jboolean CallNonvirtualBooleanMethodA(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, const jvalue* args) {
    fjni_logv_dbg("[JNI] CallNonvirtualBooleanMethodA(env, 0x%x, 0x%x, %i, args)", (int)obj, (int)clazz, methodID);
    return methodBooleanCall(methodID, _AtoV(0, args));
}

jbyte CallNonvirtualByteMethod(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, ...) {
    fjni_logv_dbg("[JNI] CallNonvirtualByteMethod(env, 0x%x, 0x%x, %i, ...)", (int)obj, (int)clazz, methodID);

    jbyte ret;
    va_list args;
    va_start(args, methodID);
    ret = methodByteCall(methodID, args);
    va_end(args);

    return ret;
}

jbyte CallNonvirtualByteMethodV(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, va_list args) {
    fjni_logv_dbg("[JNI] CallNonvirtualByteMethodV(env, 0x%x, 0x%x, %i, args)", (int)obj, (int)clazz, methodID);
    return methodByteCall(methodID, args);
}

jbyte CallNonvirtualByteMethodA(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, const jvalue* args) {
    fjni_logv_dbg("[JNI] CallNonvirtualByteMethodA(env, 0x%x, 0x%x, %i, args)", (int)obj, (int)clazz, methodID);
    return methodByteCall(methodID, _AtoV(0, args));
}

jchar CallNonvirtualCharMethod(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, ...) {
    fjni_logv_dbg("[JNI] CallNonvirtualCharMethod(env, 0x%x, 0x%x, %i, ...)", (int)obj, (int)clazz, methodID);

    jchar ret;
    va_list args;
    va_start(args, methodID);
    ret = methodCharCall(methodID, args);
    va_end(args);

    return ret;
}

jchar CallNonvirtualCharMethodV(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, va_list args) {
    fjni_logv_dbg("[JNI] CallNonvirtualCharMethodV(env, 0x%x, 0x%x, %i, args)", (int)obj, (int)clazz, methodID);
    return methodCharCall(methodID, args);
}

jchar CallNonvirtualCharMethodA(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, const jvalue* args) {
    fjni_logv_dbg("[JNI] CallNonvirtualCharMethodA(env, 0x%x, 0x%x, %i, args)", (int)obj, (int)clazz, methodID);
    return methodCharCall(methodID, _AtoV(0, args));
}

jshort CallNonvirtualShortMethod(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, ...) {
    fjni_logv_dbg("[JNI] CallNonvirtualShortMethod(env, 0x%x, 0x%x, %i, ...)", (int)obj, (int)clazz, methodID);

    jshort ret;
    va_list args;
    va_start(args, methodID);
    ret = methodShortCall(methodID, args);
    va_end(args);

    return ret;
}

jshort CallNonvirtualShortMethodV(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, va_list args) {
    fjni_logv_dbg("[JNI] CallNonvirtualShortMethodV(env, 0x%x, 0x%x, %i, args)", (int)obj, (int)clazz, methodID);
    return methodShortCall(methodID, args);
}

jshort CallNonvirtualShortMethodA(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, const jvalue* args) {
    fjni_logv_dbg("[JNI] CallNonvirtualShortMethodV(env, 0x%x, 0x%x, %i, args)", (int)obj, (int)clazz, methodID);
    return methodShortCall(methodID, _AtoV(0, args));
}

jint CallNonvirtualIntMethod(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, ...) {
    fjni_logv_dbg("[JNI] CallNonvirtualIntMethod(env, 0x%x, 0x%x, %i, ...)", (int)obj, (int)clazz, methodID);

    jint ret;
    va_list args;
    va_start(args, methodID);
    ret = methodIntCall(methodID, args);
    va_end(args);

    return ret;
}

jint CallNonvirtualIntMethodV(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, va_list args) {
    fjni_logv_dbg("[JNI] CallNonvirtualIntMethodV(env, 0x%x, 0x%x, %i, args)", (int)obj, (int)clazz, methodID);
    return methodIntCall(methodID, args);
}

jint CallNonvirtualIntMethodA(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, const jvalue* args) {
    fjni_logv_dbg("[JNI] CallNonvirtualIntMethodA(env, 0x%x, 0x%x, %i, args)", (int)obj, (int)clazz, methodID);
    return methodIntCall(methodID, _AtoV(0, args));
}

jlong CallNonvirtualLongMethod(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, ...) {
    fjni_logv_dbg("[JNI] CallNonvirtualLongMethod(env, 0x%x, 0x%x, %i, ...)", (int)obj, (int)clazz, methodID);

    jlong ret;
    va_list args;
    va_start(args, methodID);
    ret = methodLongCall(methodID, args);
    va_end(args);

    return ret;
}

jlong CallNonvirtualLongMethodV(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, va_list args) {
    fjni_logv_dbg("[JNI] CallNonvirtualLongMethodV(env, 0x%x, 0x%x, %i, args)", (int)obj, (int)clazz, methodID);
    return methodLongCall(methodID, args);
}

jlong CallNonvirtualLongMethodA(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, const jvalue* args) {
    fjni_logv_dbg("[JNI] CallNonvirtualLongMethodA(env, 0x%x, 0x%x, %i, args)", (int)obj, (int)clazz, methodID);
    return methodLongCall(methodID, _AtoV(0, args));
}

jfloat CallNonvirtualFloatMethod(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, ...) {
    fjni_logv_dbg("[JNI] CallNonvirtualFloatMethod(env, 0x%x, 0x%x, %i, ...)", (int)obj, (int)clazz, methodID);

    jfloat ret;
    va_list args;
    va_start(args, methodID);
    ret = methodFloatCall(methodID, args);
    va_end(args);

    return ret;
}

jfloat CallNonvirtualFloatMethodV(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, va_list args) {
    fjni_logv_dbg("[JNI] CallNonvirtualFloatMethodV(env, 0x%x, 0x%x, %i, args)", (int)obj, (int)clazz, methodID);
    return methodFloatCall(methodID, args);
}

jfloat CallNonvirtualFloatMethodA(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, const jvalue* args) {
    fjni_logv_dbg("[JNI] CallNonvirtualFloatMethodA(env, 0x%x, 0x%x, %i, args)", (int)obj, (int)clazz, methodID);
    return methodFloatCall(methodID, _AtoV(0, args));
}

jdouble CallNonvirtualDoubleMethod(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, ...) {
    fjni_logv_dbg("[JNI] CallNonvirtualDoubleMethod(env, 0x%x, 0x%x, %i, ...)", (int)obj, (int)clazz, methodID);

    jdouble ret;
    va_list args;
    va_start(args, methodID);
    ret = methodDoubleCall(methodID, args);
    va_end(args);

    return ret;
}

jdouble CallNonvirtualDoubleMethodV(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, va_list args) {
    fjni_logv_dbg("[JNI] CallNonvirtualDoubleMethodV(env, 0x%x, 0x%x, %i, args)", (int)obj, (int)clazz, methodID);
    return methodDoubleCall(methodID, args);
}

jdouble CallNonvirtualDoubleMethodA(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, const jvalue* args) {
    fjni_logv_dbg("[JNI] CallNonvirtualDoubleMethodA(env, 0x%x, 0x%x, %i, args)", (int)obj, (int)clazz, methodID);
    return methodDoubleCall(methodID, _AtoV(0, args));
}

void CallNonvirtualVoidMethod(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, ...) {
    fjni_logv_dbg("[JNI] CallNonvirtualVoidMethod(env, 0x%x, 0x%x, %i, ...)", (int)obj, (int)clazz, methodID);

    va_list args;
    va_start(args, methodID);
    methodVoidCall(methodID, args);
    va_end(args);
}

void CallNonvirtualVoidMethodV(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, va_list args) {
    fjni_logv_dbg("[JNI] CallNonvirtualVoidMethodV(env, 0x%x, 0x%x, %i, args)", (int)obj, (int)clazz, methodID);
    methodVoidCall(methodID, args);
}

void CallNonvirtualVoidMethodA(JNIEnv* env, jobject obj, jclass clazz, jmethodID methodID, const jvalue* args) {
    fjni_logv_dbg("[JNI] CallNonvirtualVoidMethodA(env, 0x%x, 0x%x, %i, args)", (int)obj, (int)clazz, methodID);
    methodVoidCall(methodID, _AtoV(0, args));
}

jfieldID GetFieldID(JNIEnv * env, jclass clazz, const char* name, const char* t) {
    fjni_logv_dbg("[JNI] GetFieldID(env, 0x%x, \"%s\", \"%s\")", (int)clazz, name, t);
    return getFieldIdByName(name);
}

jobject GetObjectField(JNIEnv* env, jobject obj, jfieldID fieldID) {
    fjni_logv_dbg("[JNI] GetObjectField(env, 0x%x, %i)", (int)obj, fieldID);
    return NewLocalRef(env, getObjectFieldValueById(fieldID));
}

jboolean GetBooleanField(JNIEnv* env, jobject obj, jfieldID fieldID) {
    fjni_logv_dbg("[JNI] GetBooleanField(env, 0x%x, %i)", (int)obj, fieldID);
    return getBooleanFieldValueById(fieldID);
}

jbyte GetByteField(JNIEnv* env, jobject obj, jfieldID fieldID) {
    fjni_logv_dbg("[JNI] GetByteField(env, 0x%x, %i)", (int)obj, fieldID);
    return getByteFieldValueById(fieldID);
}

jchar GetCharField(JNIEnv* env, jobject obj, jfieldID fieldID) {
    fjni_logv_dbg("[JNI] GetCharField(env, 0x%x, %i)", (int)obj, fieldID);
    return getCharFieldValueById(fieldID);
}

jshort GetShortField(JNIEnv* env, jobject obj, jfieldID fieldID) {
    fjni_logv_dbg("[JNI] GetShortField(env, 0x%x, %i)", (int)obj, fieldID);
    return getShortFieldValueById(fieldID);
}

jint GetIntField(JNIEnv* env, jobject obj, jfieldID fieldID) {
    fjni_logv_dbg("[JNI] GetIntField(env, 0x%x, %i)", (int)obj, fieldID);
    return getIntFieldValueById(fieldID);
}

jlong GetLongField(JNIEnv* env, jobject obj, jfieldID fieldID) {
    fjni_logv_dbg("[JNI] GetLongField(env, 0x%x, %i)", (int)obj, fieldID);
    return getLongFieldValueById(fieldID);
}

jfloat GetFloatField(JNIEnv* env, jobject obj, jfieldID fieldID) {
    fjni_logv_dbg("[JNI] GetFloatField(env, 0x%x, %i)", (int)obj, fieldID);
    return getFloatFieldValueById(fieldID);
}

jdouble GetDoubleField(JNIEnv* env, jobject obj, jfieldID fieldID) {
    fjni_logv_dbg("[JNI] GetFloatField(env, 0x%x, %i)", (int)obj, fieldID);
    return getDoubleFieldValueById(fieldID);
}

void SetObjectField(JNIEnv* env, jobject obj, jfieldID fieldID, jobject value) {
    fjni_logv_dbg("[JNI] SetObjectField(env, 0x%x, %i, 0x%x)", (int)obj, fieldID, (int)value);
    jobject old = getObjectFieldValueById(fieldID);
    tracked_add_ref_if_known(value, 1);
    setObjectFieldValueById(fieldID, value);
    tracked_release_ref_if_known(old, 1);
}

void SetBooleanField(JNIEnv* env, jobject obj, jfieldID fieldID, jboolean value) {
    fjni_logv_dbg("[JNI] SetBooleanField(env, 0x%x, %i, 0x%x)", (int)obj, fieldID, (int)value);
    setBooleanFieldValueById(fieldID, value);
}

void SetByteField(JNIEnv* env, jobject obj, jfieldID fieldID, jbyte value) {
    fjni_logv_dbg("[JNI] SetByteField(env, 0x%x, %i, 0x%x)", (int)obj, fieldID, (int)value);
    setByteFieldValueById(fieldID, value);
}

void SetCharField(JNIEnv* env, jobject obj, jfieldID fieldID, jchar value) {
    fjni_logv_dbg("[JNI] SetCharField(env, 0x%x, %i, '%s')", (int)obj, fieldID, value);
    setCharFieldValueById(fieldID, value);
}

void SetShortField(JNIEnv* env, jobject obj, jfieldID fieldID, jshort value) {
    fjni_logv_dbg("[JNI] SetShortField(env, 0x%x, %i, %i)", (int)obj, fieldID, (int)value);
    setShortFieldValueById(fieldID, value);
}

void SetIntField(JNIEnv* env, jobject obj, jfieldID fieldID, jint value) {
    fjni_logv_dbg("[JNI] SetIntField(env, 0x%x, %i, %i)", (int)obj, fieldID, (int)value);
    setIntFieldValueById(fieldID, value);
}

void SetLongField(JNIEnv* env, jobject obj, jfieldID fieldID, jlong value) {
    fjni_logv_dbg("[JNI] SetLongField(env, 0x%x, %i, %i)", (int)obj, fieldID, (int)value);
    setLongFieldValueById(fieldID, value);
}

void SetFloatField(JNIEnv* env, jobject obj, jfieldID fieldID, jfloat value) {
    fjni_logv_dbg("[JNI] SetFloatField(env, 0x%x, %i, %f)", (int)obj, fieldID, value);
    setFloatFieldValueById(fieldID, value);
}

void SetDoubleField(JNIEnv* env, jobject obj, jfieldID fieldID, jdouble value) {
    fjni_logv_dbg("[JNI] SetFloatField(env, 0x%x, %i, %i)", (int)obj, fieldID, value);
    setDoubleFieldValueById(fieldID, value);
}

jmethodID GetStaticMethodID(JNIEnv* env, jclass clazz, const char* _name, const char* sig) {
    jmethodID ret;
    char name[512];

    if (strcmp("<init>", _name) == 0) {
        if (!clazz) {
            fjni_log_err("Cannot find constructor method ID for class NULL");
            return NULL;
        }

        // In FindClass we return a char ptr of class name as `clazz`, so we
        // can use it here for distinguishing different constructors
        snprintf(name, sizeof(name), "%s/%s", (char*)clazz, _name);
    } else {
        snprintf(name, sizeof(name), "%s", _name);
    }

    ret = getMethodIdByName(name);

    if (ret != NULL) {
        fjni_logv_dbg("[JNI] GetStaticMethodID(env, 0x%x, \"%s\", \"%s\"): %i", (int)clazz, name, sig, (int)ret);
    } else {
        fjni_logv_warn("[JNI] GetStaticMethodID(\"%s\", \"%s\"): not in table — stubbed", name, sig);
        const char* r = sig ? strchr(sig, ')') : NULL;
        if (r && r[1] == 'Z') ret = fjni_register_boolstub(name);  /* name-tracked bool stub */
        else ret = fjni_stub_for_sig(sig);
    }

    return ret;
}

jobject CallStaticObjectMethod(JNIEnv* env, jclass clazz, jmethodID methodID, ...) {
    fjni_logv_dbg("[JNI] CallStaticObjectMethod(env, 0x%x, %i)", (int)clazz, methodID);

    jobject ret;
    va_list args;
    va_start(args, methodID);
    ret = methodObjectCall(methodID, args);
    va_end(args);

    return ret;
}

jobject CallStaticObjectMethodV(JNIEnv* env, jclass clazz, jmethodID methodID, va_list args) {
    fjni_logv_dbg("[JNI] CallStaticObjectMethodV(env, 0x%x, %i, args)", (int)clazz, (int)methodID);
    return methodObjectCall(methodID, args);
}

jobject CallStaticObjectMethodA(JNIEnv* env, jclass clazz, jmethodID methodID, const jvalue* args) {
    fjni_logv_dbg("[JNI] CallStaticObjectMethodA(env, 0x%x, %i, args)", (int)clazz, (int)methodID);
    return methodObjectCall(methodID, _AtoV(0, args));
}

jboolean CallStaticBooleanMethod(JNIEnv* env, jclass clazz, jmethodID methodID, ...) {
    fjni_logv_dbg("[JNI] CallStaticBooleanMethod(env, 0x%x, %i)", (int)clazz, methodID);

    jboolean ret;
    va_list args;
    va_start(args, methodID);
    ret = methodBooleanCall(methodID, args);
    va_end(args);

    return ret;
}

jboolean CallStaticBooleanMethodV(JNIEnv* env, jclass clazz, jmethodID methodID, va_list args) {
    fjni_logv_dbg("[JNI] CallStaticBooleanMethodV(env, 0x%x, %i, args)", (int)clazz, (int)methodID);
    return methodBooleanCall(methodID, args);
}

jboolean CallStaticBooleanMethodA(JNIEnv* env, jclass clazz, jmethodID methodID, const jvalue* args) {
    fjni_logv_dbg("[JNI] CallStaticBooleanMethodA(env, 0x%x, %i, args)", (int)clazz, (int)methodID);
    return methodBooleanCall(methodID, _AtoV(0, args));
}

jbyte CallStaticByteMethod(JNIEnv* env, jclass clazz, jmethodID methodID, ...) {
    fjni_logv_dbg("[JNI] CallStaticByteMethod(env, 0x%x, %i)", (int)clazz, methodID);

    jbyte ret;
    va_list args;
    va_start(args, methodID);
    ret = methodByteCall(methodID, args);
    va_end(args);

    return ret;
}

jbyte CallStaticByteMethodV(JNIEnv* env, jclass clazz, jmethodID methodID, va_list args) {
    fjni_logv_dbg("[JNI] CallStaticByteMethodV(env, 0x%x, %i, args)", (int)clazz, (int)methodID);
    return methodByteCall(methodID, args);
}

jbyte CallStaticByteMethodA(JNIEnv* env, jclass clazz, jmethodID methodID, const jvalue* args) {
    fjni_logv_dbg("[JNI] CallStaticByteMethodA(env, 0x%x, %i, args)", (int)clazz, (int)methodID);
    return methodByteCall(methodID, _AtoV(0, args));
}

jchar CallStaticCharMethod(JNIEnv* env, jclass clazz, jmethodID methodID, ...) {
    fjni_logv_dbg("[JNI] CallStaticCharMethod(env, 0x%x, %i)", (int)clazz, methodID);

    jchar ret;
    va_list args;
    va_start(args, methodID);
    ret = methodCharCall(methodID, args);
    va_end(args);

    return ret;
}

jchar CallStaticCharMethodV(JNIEnv* env, jclass clazz, jmethodID methodID, va_list args) {
    fjni_logv_dbg("[JNI] CallStaticCharMethodV(env, 0x%x, %i, args)", (int)clazz, (int)methodID);
    return methodCharCall(methodID, args);
}

jchar CallStaticCharMethodA(JNIEnv* env, jclass clazz, jmethodID methodID, const jvalue* args) {
    fjni_logv_dbg("[JNI] CallStaticCharMethodA(env, 0x%x, %i, args)", (int)clazz, (int)methodID);
    return methodCharCall(methodID, _AtoV(0, args));
}

jshort CallStaticShortMethod(JNIEnv* env, jclass clazz, jmethodID methodID, ...) {
    fjni_logv_dbg("[JNI] CallStaticShortMethod(env, 0x%x, %i)", (int)clazz, methodID);

    jshort ret;
    va_list args;
    va_start(args, methodID);
    ret = methodShortCall(methodID, args);
    va_end(args);

    return ret;
}

jshort CallStaticShortMethodV(JNIEnv* env, jclass clazz, jmethodID methodID, va_list args) {
    fjni_logv_dbg("[JNI] CallStaticShortMethodV(env, 0x%x, %i, args)", (int)clazz, (int)methodID);
    return methodShortCall(methodID, args);
}

jshort CallStaticShortMethodA(JNIEnv* env, jclass clazz, jmethodID methodID, const jvalue* args) {
    fjni_logv_dbg("[JNI] CallStaticShortMethodA(env, 0x%x, %i, args)", (int)clazz, (int)methodID);
    return methodShortCall(methodID, _AtoV(0, args));
}

jint CallStaticIntMethod(JNIEnv* env, jclass clazz, jmethodID methodID, ...) {
    fjni_logv_dbg("[JNI] CallStaticIntMethod(env, 0x%x, %i, ...)", (int)clazz, methodID);

    jint ret;
    va_list args;
    va_start(args, methodID);
    ret = methodIntCall(methodID, args);
    va_end(args);

    return ret;
}

jint CallStaticIntMethodV(JNIEnv* env, jclass clazz, jmethodID methodID, va_list args) {
    fjni_logv_dbg("[JNI] CallStaticIntMethodV(env, 0x%x, %i, args)", (int)clazz, methodID);
    return methodIntCall(methodID, args);
}

jint CallStaticIntMethodA(JNIEnv* env, jclass clazz, jmethodID methodID, const jvalue* args) {
    fjni_logv_dbg("[JNI] CallStaticIntMethodA(env, 0x%x, %i, args)", (int)clazz, (int)methodID);
    return methodIntCall(methodID, _AtoV(0, args));
}

jlong CallStaticLongMethod(JNIEnv* env, jclass clazz, jmethodID methodID, ...) {
    fjni_logv_dbg("[JNI] CallStaticLongMethod(env, 0x%x, %i, ...)", (int)clazz, methodID);

    jlong ret;
    va_list args;
    va_start(args, methodID);
    ret = methodLongCall(methodID, args);
    va_end(args);

    return ret;
}

jlong CallStaticLongMethodV(JNIEnv* env, jclass clazz, jmethodID methodID, va_list args) {
    fjni_logv_dbg("[JNI] CallStaticLongMethodV(env, 0x%x, %i, args)", (int)clazz, methodID);
    return methodLongCall(methodID, args);
}

jlong CallStaticLongMethodA(JNIEnv* env, jclass clazz, jmethodID methodID, const jvalue* args) {
    fjni_logv_dbg("[JNI] CallStaticLongMethodA(env, 0x%x, %i, args)", (int)clazz, (int)methodID);
    return methodLongCall(methodID, _AtoV(0, args));
}

jfloat CallStaticFloatMethod(JNIEnv* env, jclass clazz, jmethodID methodID, ...) {
    fjni_logv_dbg("[JNI] CallStaticFloatMethod(env, 0x%x, %i, ...)", (int)clazz, methodID);

    jfloat ret;
    va_list args;
    va_start(args, methodID);
    ret = methodFloatCall(methodID, args);
    va_end(args);

    return ret;
}

jfloat CallStaticFloatMethodV(JNIEnv* env, jclass clazz, jmethodID methodID, va_list args) {
    fjni_logv_dbg("[JNI] CallStaticFloatMethodV(env, 0x%x, %i, args)", (int)clazz, methodID);
    return methodFloatCall(methodID, args);
}

jfloat CallStaticFloatMethodA(JNIEnv* env, jclass clazz, jmethodID methodID, const jvalue* args) {
    fjni_logv_dbg("[JNI] CallStaticFloatMethodA(env, 0x%x, %i, args)", (int)clazz, (int)methodID);
    return methodFloatCall(methodID, _AtoV(0, args));
}

jdouble CallStaticDoubleMethod(JNIEnv* env, jclass clazz, jmethodID methodID, ...) {
    fjni_logv_dbg("[JNI] CallStaticDoubleMethod(env, 0x%x, %i, ...)", (int)clazz, methodID);

    jdouble ret;
    va_list args;
    va_start(args, methodID);
    ret = methodDoubleCall(methodID, args);
    va_end(args);

    return ret;
}

jdouble CallStaticDoubleMethodV(JNIEnv* env, jclass clazz, jmethodID methodID, va_list args) {
    fjni_logv_dbg("[JNI] CallStaticDoubleMethodV(env, 0x%x, %i, args)", (int)clazz, methodID);
    return methodDoubleCall(methodID, args);
}

jdouble CallStaticDoubleMethodA(JNIEnv* env, jclass clazz, jmethodID methodID, const jvalue* args) {
    fjni_logv_dbg("[JNI] CallStaticDoubleMethodA(env, 0x%x, %i, args)", (int)clazz, (int)methodID);
    return methodDoubleCall(methodID, _AtoV(0, args));
}

void CallStaticVoidMethod(JNIEnv* env, jclass clazz, jmethodID methodID, ...) {
    fjni_logv_dbg("[JNI] CallStaticVoidMethod(env, 0x%x, %i, ...)", (int)clazz, methodID);

    va_list args;
    va_start(args, methodID);
    methodVoidCall(methodID, args);
    va_end(args);
}

void CallStaticVoidMethodV(JNIEnv* env, jclass clazz, jmethodID methodID, va_list args) {
    fjni_logv_dbg("[JNI] CallStaticVoidMethodV(env, 0x%x, %i, args)", (int)clazz, methodID);
    methodVoidCall(methodID, args);
}

void CallStaticVoidMethodA(JNIEnv* env, jclass clazz, jmethodID methodID, const jvalue* args) {
    fjni_logv_dbg("[JNI] CallStaticVoidMethodA(env, 0x%x, %i, args)", (int)clazz, (int)methodID);
    methodVoidCall(methodID, _AtoV(0, args));
}

jfieldID GetStaticFieldID(JNIEnv* env, jclass clazz, const char* name, const char* t) {
    fjni_logv_dbg("[JNI] GetStaticFieldID(env, 0x%x, \"%s\", \"%s\")", (int)clazz, name, t);
    return getFieldIdByName(name);
}

jobject GetStaticObjectField(JNIEnv* env, jclass clazz, jfieldID fieldID) {
    fjni_logv_dbg("[JNI] GetStaticObjectField(env, 0x%x, %i)", (int)clazz, fieldID);
    return NewLocalRef(env, getObjectFieldValueById(fieldID));
}

jboolean GetStaticBooleanField(JNIEnv* env, jclass clazz, jfieldID fieldID) {
    fjni_logv_dbg("[JNI] GetStaticBooleanField(env, 0x%x, %i)", (int)clazz, fieldID);
    return getBooleanFieldValueById(fieldID);
}

jbyte GetStaticByteField(JNIEnv* env, jclass clazz, jfieldID fieldID) {
    fjni_logv_dbg("[JNI] GetStaticByteField(env, 0x%x, %i)", (int)clazz, fieldID);
    return getByteFieldValueById(fieldID);
}

jchar GetStaticCharField(JNIEnv* env, jclass clazz, jfieldID fieldID) {
    fjni_logv_dbg("[JNI] GetStaticCharField(env, 0x%x, %i)", (int)clazz, fieldID);
    return getCharFieldValueById(fieldID);
}

jshort GetStaticShortField(JNIEnv* env, jclass clazz, jfieldID fieldID) {
    fjni_logv_dbg("[JNI] GetStaticShortField(env, 0x%x, %i)", (int)clazz, fieldID);
    return getShortFieldValueById(fieldID);
}

jint GetStaticIntField(JNIEnv* env, jclass clazz, jfieldID fieldID) {
    fjni_logv_dbg("[JNI] GetStaticIntField(env, 0x%x, %i): ", (int)clazz, fieldID);
    return getIntFieldValueById(fieldID);
}

jlong GetStaticLongField(JNIEnv* env, jclass clazz, jfieldID fieldID) {
    fjni_logv_dbg("[JNI] GetStaticLongField(env, 0x%x, %i)", (int)clazz, fieldID);
    return getLongFieldValueById(fieldID);
}

jfloat GetStaticFloatField(JNIEnv* env, jclass clazz, jfieldID fieldID) {
    fjni_logv_dbg("[JNI] GetStaticFloatField(env, 0x%x, %i)", (int)clazz, fieldID);
    return getFloatFieldValueById(fieldID);
}

jdouble GetStaticDoubleField(JNIEnv* env, jclass clazz, jfieldID fieldID) {
    fjni_logv_dbg("[JNI] GetStaticDoubleField(env, 0x%x, %i)", (int)clazz, fieldID);
    return getDoubleFieldValueById(fieldID);
}

void SetStaticObjectField(JNIEnv* env, jclass clazz, jfieldID fieldID, jobject value) {
    fjni_logv_dbg("[JNI] SetStaticObjectField(env, 0x%x, %i, 0x%x)", (int)clazz, fieldID, (int)value);
    jobject old = getObjectFieldValueById(fieldID);
    tracked_add_ref_if_known(value, 1);
    setObjectFieldValueById(fieldID, value);
    tracked_release_ref_if_known(old, 1);
}

void SetStaticBooleanField(JNIEnv* env, jclass clazz, jfieldID fieldID, jboolean value) {
    fjni_logv_dbg("[JNI] SetStaticBooleanField(env, 0x%x, %i, 0x%x)", (int)clazz, fieldID, (int)value);
    setBooleanFieldValueById(fieldID, value);
}

void SetStaticByteField(JNIEnv* env, jclass clazz, jfieldID fieldID, jbyte value) {
    fjni_logv_dbg("[JNI] SetStaticByteField(env, 0x%x, %i, 0x%x)", (int)clazz, fieldID, (int)value);
    setByteFieldValueById(fieldID, value);
}

void SetStaticCharField(JNIEnv* env, jclass clazz, jfieldID fieldID, jchar value) {
    fjni_logv_dbg("[JNI] SetStaticCharField(env, 0x%x, %i, '%s')", (int)clazz, fieldID, value);
    setCharFieldValueById(fieldID, value);
}

void SetStaticShortField(JNIEnv* env, jclass clazz, jfieldID fieldID, jshort value) {
    fjni_logv_dbg("[JNI] SetStaticShortField(env, 0x%x, %i, %i)", (int)clazz, fieldID, (int)value);
    setShortFieldValueById(fieldID, value);
}

void SetStaticIntField(JNIEnv* env, jclass clazz, jfieldID fieldID, jint value) {
    fjni_logv_dbg("[JNI] SetStaticIntField(env, 0x%x, %i, %i)", (int)clazz, fieldID, (int)value);
    setIntFieldValueById(fieldID, value);
}

void SetStaticLongField(JNIEnv* env, jclass clazz, jfieldID fieldID, jlong value) {
    fjni_logv_dbg("[JNI] SetStaticLongField(env, 0x%x, %i, %i)", (int)clazz, fieldID, (int)value);
    setLongFieldValueById(fieldID, value);
}

void SetStaticFloatField(JNIEnv* env, jclass clazz, jfieldID fieldID, jfloat value) {
    fjni_logv_dbg("[JNI] SetStaticFloatField(env, 0x%x, %i, %f)", (int)clazz, fieldID, value);
    setFloatFieldValueById(fieldID, value);
}

void SetStaticDoubleField(JNIEnv* env, jclass clazz, jfieldID fieldID, jdouble value) {
    fjni_logv_dbg("[JNI] SetStaticFloatField(env, 0x%x, %i, %i)", (int)clazz, fieldID, value);
    setDoubleFieldValueById(fieldID, value);
}

/* Both representations are complete before publication and immutable thereafter.
 * JNI getters never rebuild shared buffers while another thread may read them. */
jstring NewString(JNIEnv* env, const jchar* chars, jsize char_count) {
    if (char_count < 0 || (!chars && char_count)) return NULL;
    JavaString *res = calloc(1, sizeof(*res));
    if (!res) return NULL;
    res->utf16 = jda_alloc(char_count, FIELD_TYPE_CHAR);
    if (!res->utf16) { free(res); return NULL; }
    if (char_count) memcpy(res->utf16->array, chars, (size_t)char_count * sizeof(jchar));
    if (!jstr_utf16_to_utf8(res)) {
        jda_free(res->utf16); free(res); return NULL;
    }
    return tracked_register_local_owned((jobject)res, TRACKED_OBJ_STRING);
}

jsize GetStringLength(JNIEnv* env, jstring string) {
    return string ? ((JavaString *)string)->utf16->len : 0;
}

const jchar * GetStringChars(JNIEnv* env, jstring string, jboolean *isCopy) {
    if (!string) return NULL;
    JavaString *str = string;
    size_t bytes = (size_t)str->utf16->len * sizeof(jchar);
    jchar *copy = malloc(bytes ? bytes : sizeof(jchar));
    if (!copy) return NULL;
    if (bytes) memcpy(copy, str->utf16->array, bytes);
    if (isCopy) *isCopy = JNI_TRUE;
    return copy;
}

void ReleaseStringChars(JNIEnv* env, jstring string, const jchar *chars) {
    free((void *)chars);
}

jstring NewStringUTF(JNIEnv* env, const char* bytes) {
    if (!bytes) return NULL;
    size_t size = strlen(bytes);
    if (size >= INT_MAX) return NULL;
    JavaString *res = calloc(1, sizeof(*res));
    if (!res) return NULL;
    res->utf8 = jda_alloc((jsize)size + 1, FIELD_TYPE_BYTE);
    if (!res->utf8) { free(res); return NULL; }
    memcpy(res->utf8->array, bytes, size + 1);
    /* Decode first, then canonicalize four-byte native UTF-8 to JNI's form. */
    if (!jstr_utf8_to_utf16(res) || !jstr_utf16_to_utf8(res)) {
        if (res->utf16) jda_free(res->utf16);
        jda_free(res->utf8); free(res); return NULL;
    }
    return tracked_register_local_owned((jobject)res, TRACKED_OBJ_STRING);
}

jsize GetStringUTFLength(JNIEnv* env, jstring string) {
    return string ? ((JavaString *)string)->utf8->len - 1 : 0;
}

const char* GetStringUTFChars(JNIEnv* env, jstring string, jboolean* isCopy) {
    if (!string) return NULL;
    const JavaString *str = string;
    size_t bytes = (size_t)str->utf8->len; /* Includes the terminating byte. */
    char *copy = malloc(bytes);
    if (!copy) return NULL;
    memcpy(copy, str->utf8->array, bytes);
    if (isCopy) *isCopy = JNI_TRUE;
    return copy;
}

void ReleaseStringUTFChars(JNIEnv* env, jstring string, char* chars) {
    free(chars);
}

jsize GetArrayLength(JNIEnv* env, jarray array) {
    fjni_logv_dbg("[JNI] GetArrayLength(env, 0x%x)", (int)array);

    // TODO: this can theoretically be called for ObjectField values. Need to keep track of their sizes too?
    jsize ret = jda_sizeof(array);
    if (ret > -1) return ret;

    fjni_logv_warn("Array 0x%x not found. Unknown array type?", (int)array);
    return 0;
}

jobjectArray NewObjectArray(JNIEnv* env, jsize length, jclass elementClass, jobject initialElement) {
    JavaDynArray * jda = jda_alloc(length, FIELD_TYPE_OBJECT);
    if (!jda) {
        fjni_logv_err("[JNI] NewObjectArray(env, %i, 0x%x, 0x%x): Could not allocate a new array!", length, elementClass, initialElement);
        return NULL;
    }

    jobject* arr = jda->array;
    for (int i = 0; i < length; ++i) {
        arr[i] = initialElement;
        tracked_add_ref_if_known(initialElement, 1);
    }

    fjni_logv_dbg("[JNI] NewObjectArray(env, %i, 0x%x, 0x%x): 0x%x", length, elementClass, initialElement, (int)jda);
    return tracked_register_local_owned((jobject)jda, TRACKED_OBJ_ARRAY);
}

jobject GetObjectArrayElement(JNIEnv* env, jobjectArray array, jsize index) {
    JavaDynArray * jda = (JavaDynArray *) array;
    if (!jda) {
        fjni_logv_err("[JNI] GetObjectArrayElement(env, 0x%x, idx:%i): Could not find the array", array, index);
        return NULL;
    }

    if (index >= jda->len || index < 0) {
        fjni_logv_err("[JNI] GetObjectArrayElement(env, 0x%x, idx:%i): Index out of bounds", array, index);
        return NULL;
    }

    jobject * arr = jda->array;
    fjni_logv_dbg("[JNI] GetObjectArrayElement(env, 0x%x, idx:%i): 0x%x", array, index, (int)arr[index]);
    return NewLocalRef(env, arr[index]);
}

void SetObjectArrayElement(JNIEnv* env, jobjectArray array, jsize index, jobject value) {
    JavaDynArray * jda = (JavaDynArray *) array;
    if (!jda) {
        fjni_logv_err("[JNI] SetObjectArrayElement(env, 0x%x, idx:%i, val:0x%x): Could not find the array", array, index, value);
        return;
    }

    if (index >= jda->len || index < 0) {
        fjni_logv_err("[JNI] SetObjectArrayElement(env, 0x%x, idx:%i, val:0x%x): Index out of bounds", array, index, value);
        return;
    }

    fjni_logv_dbg("[JNI] SetObjectArrayElement(env, 0x%x, idx:%i, val:0x%x)", array, index, value);

    jobject * arr = jda->array;
    jobject old = arr[index];
    tracked_add_ref_if_known(value, 1);
    arr[index] = value;
    tracked_release_ref_if_known(old, 1);
}

jbooleanArray NewBooleanArray(JNIEnv* env, jsize length) {
    JavaDynArray * jda = jda_alloc(length, FIELD_TYPE_BOOLEAN);
    if (!jda) {
        fjni_logv_err("[JNI] NewBooleanArray(env, %i): Could not allocate a new array!", length);
        return NULL;
    }

    fjni_logv_dbg("[JNI] NewBooleanArray(env, %i): 0x%x", length, (int)jda);
    return tracked_register_local_owned((jobject)jda, TRACKED_OBJ_ARRAY);
}

jbyteArray NewByteArray(JNIEnv* env, jsize length) {
    JavaDynArray * jda = jda_alloc(length, FIELD_TYPE_BYTE);
    if (!jda) {
        fjni_logv_err("[JNI] NewByteArray(env, %i): Could not allocate a new array!", length);
        return NULL;
    }

    fjni_logv_dbg("[JNI] NewByteArray(env, %i): 0x%x", length, (int)jda);
    return tracked_register_local_owned((jobject)jda, TRACKED_OBJ_ARRAY);
}

jcharArray NewCharArray(JNIEnv* env, jsize length) {
    JavaDynArray * jda = jda_alloc(length, FIELD_TYPE_CHAR);
    if (!jda) {
        fjni_logv_err("[JNI] NewCharArray(env, %i): Could not allocate a new array!", length);
        return NULL;
    }

    fjni_logv_dbg("[JNI] NewCharArray(env, %i): 0x%x", length, (int)jda);
    return tracked_register_local_owned((jobject)jda, TRACKED_OBJ_ARRAY);
}

jshortArray NewShortArray(JNIEnv* env, jsize length) {
    JavaDynArray * jda = jda_alloc(length, FIELD_TYPE_SHORT);
    if (!jda) {
        fjni_logv_err("[JNI] NewShortArray(env, %i): Could not allocate a new array!", length);
        return NULL;
    }

    fjni_logv_dbg("[JNI] NewShortArray(env, %i): 0x%x", length, (int)jda);
    return tracked_register_local_owned((jobject)jda, TRACKED_OBJ_ARRAY);
}

jintArray NewIntArray(JNIEnv* env, jsize length) {
    JavaDynArray * jda = jda_alloc(length, FIELD_TYPE_INT);
    if (!jda) {
        fjni_logv_err("[JNI] NewIntArray(env, %i): Could not allocate a new array!", length);
        return NULL;
    }

    fjni_logv_dbg("[JNI] NewIntArray(env, %i): 0x%x", length, (int)jda);
    return tracked_register_local_owned((jobject)jda, TRACKED_OBJ_ARRAY);
}

jlongArray NewLongArray(JNIEnv* env, jsize length) {
    JavaDynArray * jda = jda_alloc(length, FIELD_TYPE_LONG);
    if (!jda) {
        fjni_logv_err("[JNI] NewLongArray(env, %i): Could not allocate a new array!", length);
        return NULL;
    }

    fjni_logv_dbg("[JNI] NewLongArray(env, %i): 0x%x", length, (int)jda);
    return tracked_register_local_owned((jobject)jda, TRACKED_OBJ_ARRAY);
}

jfloatArray NewFloatArray(JNIEnv* env, jsize length) {
    JavaDynArray * jda = jda_alloc(length, FIELD_TYPE_FLOAT);
    if (!jda) {
        fjni_logv_err("[JNI] NewFloatArray(env, %i): Could not allocate a new array!", length);
        return NULL;
    }

    fjni_logv_dbg("[JNI] NewFloatArray(env, %i): 0x%x", length, (int)jda);
    return tracked_register_local_owned((jobject)jda, TRACKED_OBJ_ARRAY);
}

jdoubleArray NewDoubleArray(JNIEnv* env, jsize length) {
    JavaDynArray * jda = jda_alloc(length, FIELD_TYPE_DOUBLE);
    if (!jda) {
        fjni_logv_err("[JNI] NewDoubleArray(env, %i): Could not allocate a new array!", length);
        return NULL;
    }

    fjni_logv_dbg("[JNI] NewDoubleArray(env, %i): 0x%x", length, (int)jda);
    return tracked_register_local_owned((jobject)jda, TRACKED_OBJ_ARRAY);
}

jboolean* GetBooleanArrayElements(JNIEnv* env, jbooleanArray array, jboolean* isCopy) {
    JavaDynArray * jda = (JavaDynArray *) array;
    if (!jda) {
        fjni_logv_err("[JNI] GetBooleanArrayElements(env, 0x%x, 0x%x): Could not find the array", array, isCopy);
        return NULL;
    }

    fjni_logv_dbg("[JNI] GetBooleanArrayElements(env, 0x%x, 0x%x)", (int)array, (int)isCopy);
    if (isCopy != NULL) *isCopy = JNI_FALSE;
    return jda->array;
}

jbyte* GetByteArrayElements(JNIEnv* env, jbyteArray array, jboolean* isCopy) {
    JavaDynArray * jda = (JavaDynArray *) array;
    if (!jda) {
        fjni_logv_err("[JNI] GetByteArrayElements(env, 0x%x, 0x%x): Could not find the array", array, isCopy);
        return NULL;
    }

    fjni_logv_dbg("[JNI] GetByteArrayElements(env, 0x%x, 0x%x)", (int)array, (int)isCopy);
    if (isCopy != NULL) *isCopy = JNI_FALSE;
    return jda->array;
}

jchar* GetCharArrayElements(JNIEnv* env, jcharArray array, jboolean* isCopy) {
    JavaDynArray * jda = (JavaDynArray *) array;
    if (!jda) {
        fjni_logv_err("[JNI] GetCharArrayElements(env, 0x%x, 0x%x): Could not find the array", array, isCopy);
        return NULL;
    }

    fjni_logv_dbg("[JNI] GetCharArrayElements(env, 0x%x, 0x%x)", (int)array, (int)isCopy);
    if (isCopy != NULL) *isCopy = JNI_FALSE;
    return jda->array;
}

jshort* GetShortArrayElements(JNIEnv* env, jshortArray array, jboolean* isCopy) {
    JavaDynArray * jda = (JavaDynArray *) array;
    if (!jda) {
        fjni_logv_err("[JNI] GetShortArrayElements(env, 0x%x, 0x%x): Could not find the array", array, isCopy);
        return NULL;
    }

    fjni_logv_dbg("[JNI] GetShortArrayElements(env, 0x%x, 0x%x)", (int)array, (int)isCopy);
    if (isCopy != NULL) *isCopy = JNI_FALSE;
    return jda->array;
}

jint* GetIntArrayElements(JNIEnv* env, jintArray array, jboolean* isCopy) {
    JavaDynArray * jda = (JavaDynArray *) array;
    if (!jda) {
        fjni_logv_err("[JNI] GetIntArrayElements(env, 0x%x, 0x%x): Could not find the array", array, isCopy);
        return NULL;
    }

    fjni_logv_dbg("[JNI] GetIntArrayElements(env, 0x%x, 0x%x)", (int)array, (int)isCopy);
    if (isCopy != NULL) *isCopy = JNI_FALSE;
    return jda->array;
}

jlong* GetLongArrayElements(JNIEnv* env, jlongArray array, jboolean* isCopy) {
    JavaDynArray * jda = (JavaDynArray *) array;
    if (!jda) {
        fjni_logv_err("[JNI] GetLongArrayElements(env, 0x%x, 0x%x): Could not find the array", array, isCopy);
        return NULL;
    }

    fjni_logv_dbg("[JNI] GetLongArrayElements(env, 0x%x, 0x%x)", (int)array, (int)isCopy);
    if (isCopy != NULL) *isCopy = JNI_FALSE;
    return jda->array;
}

jfloat* GetFloatArrayElements(JNIEnv* env, jfloatArray array, jboolean* isCopy) {
    JavaDynArray * jda = (JavaDynArray *) array;
    if (!jda) {
        fjni_logv_err("[JNI] GetFloatArrayElements(env, 0x%x, 0x%x): Could not find the array", array, isCopy);
        return NULL;
    }

    fjni_logv_dbg("[JNI] GetFloatArrayElements(env, 0x%x, 0x%x)", (int)array, (int)isCopy);
    if (isCopy != NULL) *isCopy = JNI_FALSE;
    return jda->array;
}

jdouble* GetDoubleArrayElements(JNIEnv* env, jdoubleArray array, jboolean* isCopy) {
    JavaDynArray * jda = (JavaDynArray *) array;
    if (!jda) {
        fjni_logv_err("[JNI] GetDoubleArrayElements(env, 0x%x, 0x%x): Could not find the array", array, isCopy);
        return NULL;
    }

    fjni_logv_dbg("[JNI] GetDoubleArrayElements(env, 0x%x, 0x%x)", (int)array, (int)isCopy);
    if (isCopy != NULL) *isCopy = JNI_FALSE;
    return jda->array;
}

// In Get<type>ArrayElements we never make copies, so Release<type>ArrayElements can be ignored

void ReleaseBooleanArrayElements(JNIEnv* env, jbooleanArray array, jboolean* elems, jint mode) { fjni_log_dbg("[JNI] ReleaseBooleanArrayElements(): ignored"); }
void ReleaseByteArrayElements(JNIEnv* env, jbyteArray array, jbyte* elems, jint mode) { fjni_log_dbg("[JNI] ReleaseByteArrayElements(): ignored"); }
void ReleaseCharArrayElements(JNIEnv* env, jcharArray array, jchar* elems, jint mode) { fjni_log_dbg("[JNI] ReleaseCharArrayElements(): ignored"); }
void ReleaseShortArrayElements(JNIEnv* env, jshortArray array, jshort* elems, jint mode) { fjni_log_dbg("[JNI] ReleaseShortArrayElements(): ignored"); }
void ReleaseIntArrayElements(JNIEnv* env, jintArray array, jint* elems, jint mode) { fjni_log_dbg("[JNI] ReleaseIntArrayElements(): ignored"); }
void ReleaseLongArrayElements(JNIEnv* env, jlongArray array, jlong* elems, jint mode) { fjni_log_dbg("[JNI] ReleaseLongArrayElements(): ignored"); }
void ReleaseFloatArrayElements(JNIEnv* env, jfloatArray array, jfloat* elems, jint mode) { fjni_log_dbg("[JNI] ReleaseFloatArrayElements(): ignored"); }
void ReleaseDoubleArrayElements(JNIEnv* env, jdoubleArray array, jdouble* elems, jint mode) { fjni_log_dbg("[JNI] ReleaseDoubleArrayElements(): ignored"); }

void GetBooleanArrayRegion(JNIEnv* env, jbooleanArray array, jsize start, jsize length, jboolean* buffer) {
    GetPrimitiveArrayRegion("GetBooleanArrayRegion", FIELD_TYPE_BOOLEAN, jboolean, array, start, length, buffer);
}

void GetByteArrayRegion(JNIEnv* env, jbyteArray array, jsize start, jsize length, jbyte* buffer) {
    GetPrimitiveArrayRegion("GetByteArrayRegion", FIELD_TYPE_BYTE, jbyte, array, start, length, buffer);
}

void GetCharArrayRegion(JNIEnv* env, jcharArray array, jsize start, jsize length, jchar* buffer) {
    GetPrimitiveArrayRegion("GetCharArrayRegion", FIELD_TYPE_CHAR, jchar, array, start, length, buffer);
}

void GetShortArrayRegion(JNIEnv* env, jshortArray array, jsize start, jsize length, jshort* buffer) {
    GetPrimitiveArrayRegion("GetShortArrayRegion", FIELD_TYPE_SHORT, jshort, array, start, length, buffer);
}

void GetIntArrayRegion(JNIEnv* env, jintArray array, jsize start, jsize length, jint* buffer) {
    GetPrimitiveArrayRegion("GetIntArrayRegion", FIELD_TYPE_INT, jint, array, start, length, buffer);
}

void GetLongArrayRegion(JNIEnv* env, jlongArray array, jsize start, jsize length, jlong* buffer) {
    GetPrimitiveArrayRegion("GetLongArrayRegion", FIELD_TYPE_LONG, jlong, array, start, length, buffer);
}

void GetFloatArrayRegion(JNIEnv* env, jfloatArray array, jsize start, jsize length, jfloat* buffer) {
    GetPrimitiveArrayRegion("GetFloatArrayRegion", FIELD_TYPE_FLOAT, jfloat, array, start, length, buffer);
}

void GetDoubleArrayRegion(JNIEnv* env, jdoubleArray array, jsize start, jsize length, jdouble* buffer) {
    GetPrimitiveArrayRegion("GetDoubleArrayRegion", FIELD_TYPE_DOUBLE, jdouble, array, start, length, buffer);
}

void SetBooleanArrayRegion(JNIEnv* env, jbooleanArray array, jsize start, jsize len, const jboolean* buf) {
    SetPrimitiveArrayRegion("SetBooleanArrayRegion", FIELD_TYPE_BOOLEAN, jboolean, array, start, len, buf);
}

void SetByteArrayRegion(JNIEnv* env, jbyteArray array, jsize start, jsize len, const jbyte* buf) {
    SetPrimitiveArrayRegion("SetByteArrayRegion", FIELD_TYPE_BYTE, jbyte, array, start, len, buf);
}

void SetCharArrayRegion(JNIEnv* env, jcharArray array, jsize start, jsize len, const jchar* buf) {
    SetPrimitiveArrayRegion("SetCharArrayRegion", FIELD_TYPE_CHAR, jchar, array, start, len, buf);
}

void SetShortArrayRegion(JNIEnv* env, jshortArray array, jsize start, jsize len, const jshort* buf) {
    SetPrimitiveArrayRegion("SetShortArrayRegion", FIELD_TYPE_SHORT, jshort, array, start, len, buf);
}

void SetIntArrayRegion(JNIEnv* env, jintArray array, jsize start, jsize len, const jint* buf) {
    SetPrimitiveArrayRegion("SetIntArrayRegion", FIELD_TYPE_INT, jint, array, start, len, buf);
}

void SetLongArrayRegion(JNIEnv* env, jlongArray array, jsize start, jsize len, const jlong* buf) {
    SetPrimitiveArrayRegion("SetLongArrayRegion", FIELD_TYPE_LONG, jlong, array, start, len, buf);
}

void SetFloatArrayRegion(JNIEnv* env, jfloatArray array, jsize start, jsize len, const jfloat* buf) {
    SetPrimitiveArrayRegion("SetFloatArrayRegion", FIELD_TYPE_FLOAT, jfloat, array, start, len, buf);
}

void SetDoubleArrayRegion(JNIEnv* env, jdoubleArray array, jsize start, jsize len, const jdouble* buf) {
    SetPrimitiveArrayRegion("SetDoubleArrayRegion", FIELD_TYPE_DOUBLE, jdouble, array, start, len, buf);
}

// Due to the way we define and execute functions, Register/UnregisterNatives are redundant

/* --- Native registry -----------------------------------------------------
 * PvZ2 (SexyAppFramework) registers its engine natives here and expects the
 * Java side to call them. There is no Java side, so WE call them (see
 * drive_engine in main.c). Capture name->fnPtr for lookup by name. The name
 * pointers live in libPVZ2's rodata (stays mapped), so storing them is safe. */
typedef struct { const char *name; void *fnPtr; } FjniNative;
#define FJNI_MAX_NATIVES 1024
static FjniNative g_fjni_natives[FJNI_MAX_NATIVES];
static int g_fjni_native_count = 0;

void *falso_jni_get_native(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < g_fjni_native_count; i++) {
        if (g_fjni_natives[i].name && strcmp(g_fjni_natives[i].name, name) == 0) {
            return g_fjni_natives[i].fnPtr;
        }
    }
    return NULL;
}

jint RegisterNatives(JNIEnv* env, jclass clazz, const JNINativeMethod* methods, jint nMethods) {
    /* Log at ERROR level (build filters INFO), AND capture each native so the
     * loader can call PvZ2's engine entry points by name. */
    fjni_logv_err("[RN] RegisterNatives(class=0x%x, n=%i):", (int)clazz, nMethods);
    if (methods) {
        for (jint i = 0; i < nMethods; i++) {
            fjni_logv_err("[RN]   native[%i] %s %s -> 0x%08x",
                          i,
                          methods[i].name ? methods[i].name : "(null)",
                          methods[i].signature ? methods[i].signature : "(null)",
                          (unsigned int)methods[i].fnPtr);
            if (g_fjni_native_count < FJNI_MAX_NATIVES && methods[i].name) {
                g_fjni_natives[g_fjni_native_count].name  = methods[i].name;
                g_fjni_natives[g_fjni_native_count].fnPtr = methods[i].fnPtr;
                g_fjni_native_count++;
            }
        }
    }
    return JNI_OK;
}

jint UnregisterNatives(JNIEnv* env, jclass clazz) {
    fjni_logv_dbg("[JNI] UnregisterNatives(env, 0x%x): ignored", (int)clazz);
    return JNI_OK;
}

// TODO: Implement MonitorEnter/MonitorExit with semaphores?

jint MonitorEnter(JNIEnv* env, jobject obj) {
    // Here reduced log level to dbg to avoid a million logs
    fjni_logv_dbg("[JNI] MonitorEnter(env, 0x%x): not implemented", (int)obj);
    return JNI_OK;
}

jint MonitorExit(JNIEnv* env, jobject obj) {
    // Here reduced log level to dbg to avoid a million logs
    fjni_logv_dbg("[JNI] MonitorExit(env, 0x%x): not implemented", (int)obj);
    return JNI_OK;
}

jint GetJavaVM(JNIEnv* env, JavaVM** vm) {
    fjni_log_dbg("[JNI] GetJavaVM(env, *vm)");
    *vm = &jvm;
    return JNI_OK;
}

void GetStringRegion(JNIEnv* env, jstring str, jsize start, jsize len, jchar* buf) {
    if (!str || !buf) return;
    const JavaString *string = str;
    if (start < 0 || len < 0 || start > string->utf16->len || len > string->utf16->len - start) {
        fjni_logv_err("%s", "[JNI] GetStringRegion: StringIndexOutOfBoundsException");
        return;
    }
    if (len) memcpy(buf, (const jchar *)string->utf16->array + start, (size_t)len * sizeof(jchar));
}

void GetStringUTFRegion(JNIEnv* env, jstring str, jsize start, jsize len, char* buf) {
    if (!str || !buf) return;
    const JavaString *string = str;
    if (start < 0 || len < 0 || start > string->utf16->len || len > string->utf16->len - start) {
        fjni_logv_err("%s", "[JNI] GetStringUTFRegion: StringIndexOutOfBoundsException");
        return;
    }
    /* Region indices are UTF-16 units, not UTF-8 byte offsets. JNI does not
     * require a terminator here; write only the requested encoded region. */
    fjni_mutf8_encode((const uint16_t *)string->utf16->array + start, (size_t)len, buf);
}

void* GetPrimitiveArrayCritical(JNIEnv* env, jarray array, jboolean* isCopy) {
    if (isCopy) *isCopy = JNI_FALSE;

    JavaDynArray * jda = (JavaDynArray *) array;
    if (!jda) {
        fjni_logv_err("[JNI] GetPrimitiveArrayCritical(env, 0x%x, 0x%x): Array not found.", (int)array, (int)isCopy);
        return NULL;
    }

    fjni_logv_dbg("[JNI] GetPrimitiveArrayCritical(env, 0x%x, 0x%x)", (int)array, (int)isCopy);
    return jda->array;
}

void ReleasePrimitiveArrayCritical(JNIEnv* env, jarray array, void* carray, jint mode) {
    // We never copy in GetPrimitiveArrayCritical, so can ignore Release*
    fjni_logv_dbg("[JNI] ReleasePrimitiveArrayCritical(env, 0x%x, 0x%x, %i): ignored", (int)array, (int)carray, mode);
}

const jchar* GetStringCritical(JNIEnv* env, jstring string, jboolean* isCopy) {
    fjni_logv_dbg("[JNI] GetStringCritical(env, %p, *isCopy)", string);

    if (!string) {
        fjni_logv_err("[JNI] GetStringCritical(env, %p, *isCopy): string is null", string);
        return NULL;
    }

    if (isCopy != NULL) {
        *isCopy = JNI_FALSE;
    }

    return ((JavaString *)string)->utf16->array;
}

void ReleaseStringCritical(JNIEnv* env, jstring string, const jchar* carray) {
    fjni_logv_dbg("[JNI] ReleaseStringCritical(env, %p, %p)", string, carray);

    // We never issue copies in GetStringCritical => do nothing here
}

jweak NewWeakGlobalRef(JNIEnv* env, jobject obj) {
    fjni_logv_dbg("[JNI] NewWeakGlobalRef(env, 0x%x): ignored", (int)obj);
    tracked_add_ref_if_known(obj, 1);
    return (jweak)obj;
}

void DeleteWeakGlobalRef(JNIEnv* env, jweak obj) {
    fjni_logv_dbg("[JNI] DeleteWeakGlobalRef(env, 0x%x)", (int)obj);
    tracked_release_ref_if_known((jobject)obj, 1);
}

jboolean ExceptionCheck(JNIEnv* env) {
    fjni_log_dbg("[JNI] ExceptionCheck(env): ignored");
    return JNI_FALSE;
}

/* Direct ByteBuffers: model the ByteBuffer object as its own native address.
 * Returning NULL from NewDirectByteBuffer made the engine's C++ ByteBuffer
 * wrapper operate on a null base and query a bogus capacity. We hand back the
 * address as the jobject and recover it (and the capacity) in the accessors. A
 * small ring remembers the capacity of the most-recent buffers. */
#define FJNI_DBB_SLOTS 256
static struct { void *addr; jlong cap; } s_dbb[FJNI_DBB_SLOTS];
static volatile int s_dbb_head = 0;

jobject NewDirectByteBuffer(JNIEnv* env, void* address, jlong capacity) {
    if (!address) return NULL;
    int i = s_dbb_head;
    s_dbb[i].addr = address;
    s_dbb[i].cap  = capacity;
    s_dbb_head = (i + 1) % FJNI_DBB_SLOTS;
    fjni_log_dbg("[JNI] NewDirectByteBuffer -> address as jobject");
    return (jobject)address;
}

void* GetDirectBufferAddress(JNIEnv* env, jobject buf) {
    return (void*)buf;
}

jlong GetDirectBufferCapacity(JNIEnv* env, jobject buf) {
    for (int i = 0; i < FJNI_DBB_SLOTS; i++)
        if (s_dbb[i].addr == (void*)buf)
            return s_dbb[i].cap;
    return 0;
}

jobjectRefType GetObjectRefType(JNIEnv* env, jobject obj) {
    fjni_logv_warn("[JNI] GetObjectRefType(env, 0x%x): not implemented", (int)obj);
    return JNIInvalidRefType;
}

void jni_init() {
    _jvm = (struct JNIInvokeInterface *) malloc(sizeof(struct JNIInvokeInterface));
    _jvm->DestroyJavaVM = DestroyJavaVM;
    _jvm->AttachCurrentThread = AttachCurrentThread;
    _jvm->DetachCurrentThread = DetachCurrentThread;
    _jvm->GetEnv = GetEnv;
    _jvm->AttachCurrentThreadAsDaemon = AttachCurrentThreadAsDaemon;

    _jni = (struct JNINativeInterface *) malloc(sizeof(struct JNINativeInterface));
    _jni->GetVersion = GetVersion;
    _jni->DefineClass = DefineClass;
    _jni->FindClass = FindClass;
    _jni->FromReflectedMethod = FromReflectedMethod;
    _jni->FromReflectedField = FromReflectedField;
    _jni->ToReflectedMethod = ToReflectedMethod;
    _jni->GetSuperclass = GetSuperclass;
    _jni->IsAssignableFrom = IsAssignableFrom;
    _jni->ToReflectedField = ToReflectedField;
    _jni->Throw = Throw;
    _jni->ThrowNew = ThrowNew;
    _jni->ExceptionOccurred = ExceptionOccurred;
    _jni->ExceptionDescribe = ExceptionDescribe;
    _jni->ExceptionClear = ExceptionClear;
    _jni->FatalError = FatalError;
    _jni->PushLocalFrame = PushLocalFrame;
    _jni->PopLocalFrame = PopLocalFrame;
    _jni->NewGlobalRef = NewGlobalRef;
    _jni->DeleteGlobalRef = DeleteGlobalRef;
    _jni->DeleteLocalRef = DeleteLocalRef;
    _jni->IsSameObject = IsSameObject;
    _jni->NewLocalRef = NewLocalRef;
    _jni->EnsureLocalCapacity = EnsureLocalCapacity;
    _jni->AllocObject = AllocObject;
    _jni->NewObject = NewObject;
    _jni->NewObjectV = NewObjectV;
    _jni->NewObjectA = NewObjectA;
    _jni->GetObjectClass = GetObjectClass;
    _jni->IsInstanceOf = IsInstanceOf;
    _jni->GetMethodID = GetMethodID;
    _jni->CallObjectMethod = CallObjectMethod;
    _jni->CallObjectMethodV = CallObjectMethodV;
    _jni->CallObjectMethodA = CallObjectMethodA;
    _jni->CallBooleanMethod = CallBooleanMethod;
    _jni->CallBooleanMethodV = CallBooleanMethodV;
    _jni->CallBooleanMethodA = CallBooleanMethodA;
    _jni->CallByteMethod = CallByteMethod;
    _jni->CallByteMethodV = CallByteMethodV;
    _jni->CallByteMethodA = CallByteMethodA;
    _jni->CallCharMethod = CallCharMethod;
    _jni->CallCharMethodV = CallCharMethodV;
    _jni->CallCharMethodA = CallCharMethodA;
    _jni->CallShortMethod = CallShortMethod;
    _jni->CallShortMethodV = CallShortMethodV;
    _jni->CallShortMethodA = CallShortMethodA;
    _jni->CallIntMethod = CallIntMethod;
    _jni->CallIntMethodV = CallIntMethodV;
    _jni->CallIntMethodA = CallIntMethodA;
    _jni->CallLongMethod = CallLongMethod;
    _jni->CallLongMethodV = CallLongMethodV;
    _jni->CallLongMethodA = CallLongMethodA;
    _jni->CallFloatMethod = CallFloatMethod;
    _jni->CallFloatMethodV = CallFloatMethodV;
    _jni->CallFloatMethodA = CallFloatMethodA;
    _jni->CallDoubleMethod = CallDoubleMethod;
    _jni->CallDoubleMethodV = CallDoubleMethodV;
    _jni->CallDoubleMethodA = CallDoubleMethodA;
    _jni->CallVoidMethod = CallVoidMethod;
    _jni->CallVoidMethodV = CallVoidMethodV;
    _jni->CallVoidMethodA = CallVoidMethodA;
    _jni->CallNonvirtualObjectMethod = CallNonvirtualObjectMethod;
    _jni->CallNonvirtualObjectMethodV = CallNonvirtualObjectMethodV;
    _jni->CallNonvirtualObjectMethodA = CallNonvirtualObjectMethodA;
    _jni->CallNonvirtualBooleanMethod = CallNonvirtualBooleanMethod;
    _jni->CallNonvirtualBooleanMethodV = CallNonvirtualBooleanMethodV;
    _jni->CallNonvirtualBooleanMethodA = CallNonvirtualBooleanMethodA;
    _jni->CallNonvirtualByteMethod = CallNonvirtualByteMethod;
    _jni->CallNonvirtualByteMethodV = CallNonvirtualByteMethodV;
    _jni->CallNonvirtualByteMethodA = CallNonvirtualByteMethodA;
    _jni->CallNonvirtualCharMethod = CallNonvirtualCharMethod;
    _jni->CallNonvirtualCharMethodV = CallNonvirtualCharMethodV;
    _jni->CallNonvirtualCharMethodA = CallNonvirtualCharMethodA;
    _jni->CallNonvirtualShortMethod = CallNonvirtualShortMethod;
    _jni->CallNonvirtualShortMethodV = CallNonvirtualShortMethodV;
    _jni->CallNonvirtualShortMethodA = CallNonvirtualShortMethodA;
    _jni->CallNonvirtualIntMethod = CallNonvirtualIntMethod;
    _jni->CallNonvirtualIntMethodV = CallNonvirtualIntMethodV;
    _jni->CallNonvirtualIntMethodA = CallNonvirtualIntMethodA;
    _jni->CallNonvirtualLongMethod = CallNonvirtualLongMethod;
    _jni->CallNonvirtualLongMethodV = CallNonvirtualLongMethodV;
    _jni->CallNonvirtualLongMethodA = CallNonvirtualLongMethodA;
    _jni->CallNonvirtualFloatMethod = CallNonvirtualFloatMethod;
    _jni->CallNonvirtualFloatMethodV = CallNonvirtualFloatMethodV;
    _jni->CallNonvirtualFloatMethodA = CallNonvirtualFloatMethodA;
    _jni->CallNonvirtualDoubleMethod = CallNonvirtualDoubleMethod;
    _jni->CallNonvirtualDoubleMethodV = CallNonvirtualDoubleMethodV;
    _jni->CallNonvirtualDoubleMethodA = CallNonvirtualDoubleMethodA;
    _jni->CallNonvirtualVoidMethod = CallNonvirtualVoidMethod;
    _jni->CallNonvirtualVoidMethodV = CallNonvirtualVoidMethodV;
    _jni->CallNonvirtualVoidMethodA = CallNonvirtualVoidMethodA;
    _jni->GetFieldID = GetFieldID;
    _jni->GetObjectField = GetObjectField;
    _jni->GetBooleanField = GetBooleanField;
    _jni->GetByteField = GetByteField;
    _jni->GetCharField = GetCharField;
    _jni->GetShortField = GetShortField;
    _jni->GetIntField = GetIntField;
    _jni->GetLongField = GetLongField;
    _jni->GetFloatField = GetFloatField;
    _jni->GetDoubleField = GetDoubleField;
    _jni->SetObjectField = SetObjectField;
    _jni->SetBooleanField = SetBooleanField;
    _jni->SetByteField = SetByteField;
    _jni->SetCharField = SetCharField;
    _jni->SetShortField = SetShortField;
    _jni->SetIntField = SetIntField;
    _jni->SetLongField = SetLongField;
    _jni->SetFloatField = SetFloatField;
    _jni->SetDoubleField = SetDoubleField;
    _jni->GetStaticMethodID = GetStaticMethodID;
    _jni->CallStaticObjectMethod = CallStaticObjectMethod;
    _jni->CallStaticObjectMethodV = CallStaticObjectMethodV;
    _jni->CallStaticObjectMethodA = CallStaticObjectMethodA;
    _jni->CallStaticBooleanMethod = CallStaticBooleanMethod;
    _jni->CallStaticBooleanMethodV = CallStaticBooleanMethodV;
    _jni->CallStaticBooleanMethodA = CallStaticBooleanMethodA;
    _jni->CallStaticByteMethod = CallStaticByteMethod;
    _jni->CallStaticByteMethodV = CallStaticByteMethodV;
    _jni->CallStaticByteMethodA = CallStaticByteMethodA;
    _jni->CallStaticCharMethod = CallStaticCharMethod;
    _jni->CallStaticCharMethodV = CallStaticCharMethodV;
    _jni->CallStaticCharMethodA = CallStaticCharMethodA;
    _jni->CallStaticShortMethod = CallStaticShortMethod;
    _jni->CallStaticShortMethodV = CallStaticShortMethodV;
    _jni->CallStaticShortMethodA = CallStaticShortMethodA;
    _jni->CallStaticIntMethod = CallStaticIntMethod;
    _jni->CallStaticIntMethodV = CallStaticIntMethodV;
    _jni->CallStaticIntMethodA = CallStaticIntMethodA;
    _jni->CallStaticLongMethod = CallStaticLongMethod;
    _jni->CallStaticLongMethodV = CallStaticLongMethodV;
    _jni->CallStaticLongMethodA = CallStaticLongMethodA;
    _jni->CallStaticFloatMethod = CallStaticFloatMethod;
    _jni->CallStaticFloatMethodV = CallStaticFloatMethodV;
    _jni->CallStaticFloatMethodA = CallStaticFloatMethodA;
    _jni->CallStaticDoubleMethod = CallStaticDoubleMethod;
    _jni->CallStaticDoubleMethodV = CallStaticDoubleMethodV;
    _jni->CallStaticDoubleMethodA = CallStaticDoubleMethodA;
    _jni->CallStaticVoidMethod = CallStaticVoidMethod;
    _jni->CallStaticVoidMethodV = CallStaticVoidMethodV;
    _jni->CallStaticVoidMethodA = CallStaticVoidMethodA;
    _jni->GetStaticFieldID = GetStaticFieldID;
    _jni->GetStaticObjectField = GetStaticObjectField;
    _jni->GetStaticBooleanField = GetStaticBooleanField;
    _jni->GetStaticByteField = GetStaticByteField;
    _jni->GetStaticCharField = GetStaticCharField;
    _jni->GetStaticShortField = GetStaticShortField;
    _jni->GetStaticIntField = GetStaticIntField;
    _jni->GetStaticLongField = GetStaticLongField;
    _jni->GetStaticFloatField = GetStaticFloatField;
    _jni->GetStaticDoubleField = GetStaticDoubleField;
    _jni->SetStaticObjectField = SetStaticObjectField;
    _jni->SetStaticBooleanField = SetStaticBooleanField;
    _jni->SetStaticByteField = SetStaticByteField;
    _jni->SetStaticCharField = SetStaticCharField;
    _jni->SetStaticShortField = SetStaticShortField;
    _jni->SetStaticIntField = SetStaticIntField;
    _jni->SetStaticLongField = SetStaticLongField;
    _jni->SetStaticFloatField = SetStaticFloatField;
    _jni->SetStaticDoubleField = SetStaticDoubleField;
    _jni->NewString = NewString;
    _jni->GetStringLength = GetStringLength;
    _jni->GetStringChars = GetStringChars;
    _jni->ReleaseStringChars = ReleaseStringChars;
    _jni->NewStringUTF = NewStringUTF;
    _jni->GetStringUTFLength = GetStringUTFLength;
    _jni->GetStringUTFChars = GetStringUTFChars;
    _jni->ReleaseStringUTFChars = ReleaseStringUTFChars;
    _jni->GetArrayLength = GetArrayLength;
    _jni->NewObjectArray = NewObjectArray;
    _jni->GetObjectArrayElement = GetObjectArrayElement;
    _jni->SetObjectArrayElement = SetObjectArrayElement;
    _jni->NewBooleanArray = NewBooleanArray;
    _jni->NewByteArray = NewByteArray;
    _jni->NewCharArray = NewCharArray;
    _jni->NewShortArray = NewShortArray;
    _jni->NewIntArray = NewIntArray;
    _jni->NewLongArray = NewLongArray;
    _jni->NewFloatArray = NewFloatArray;
    _jni->NewDoubleArray = NewDoubleArray;
    _jni->GetBooleanArrayElements = GetBooleanArrayElements;
    _jni->GetByteArrayElements = GetByteArrayElements;
    _jni->GetCharArrayElements = GetCharArrayElements;
    _jni->GetShortArrayElements = GetShortArrayElements;
    _jni->GetIntArrayElements = GetIntArrayElements;
    _jni->GetLongArrayElements = GetLongArrayElements;
    _jni->GetFloatArrayElements = GetFloatArrayElements;
    _jni->GetDoubleArrayElements = GetDoubleArrayElements;
    _jni->ReleaseBooleanArrayElements = ReleaseBooleanArrayElements;
    _jni->ReleaseByteArrayElements = ReleaseByteArrayElements;
    _jni->ReleaseCharArrayElements = ReleaseCharArrayElements;
    _jni->ReleaseShortArrayElements = ReleaseShortArrayElements;
    _jni->ReleaseIntArrayElements = ReleaseIntArrayElements;
    _jni->ReleaseLongArrayElements = ReleaseLongArrayElements;
    _jni->ReleaseFloatArrayElements = ReleaseFloatArrayElements;
    _jni->ReleaseDoubleArrayElements = ReleaseDoubleArrayElements;
    _jni->GetBooleanArrayRegion = GetBooleanArrayRegion;
    _jni->GetByteArrayRegion = GetByteArrayRegion;
    _jni->GetCharArrayRegion = GetCharArrayRegion;
    _jni->GetShortArrayRegion = GetShortArrayRegion;
    _jni->GetIntArrayRegion = GetIntArrayRegion;
    _jni->GetLongArrayRegion = GetLongArrayRegion;
    _jni->GetFloatArrayRegion = GetFloatArrayRegion;
    _jni->GetDoubleArrayRegion = GetDoubleArrayRegion;
    _jni->SetBooleanArrayRegion = SetBooleanArrayRegion;
    _jni->SetByteArrayRegion = SetByteArrayRegion;
    _jni->SetCharArrayRegion = SetCharArrayRegion;
    _jni->SetShortArrayRegion = SetShortArrayRegion;
    _jni->SetIntArrayRegion = SetIntArrayRegion;
    _jni->SetLongArrayRegion = SetLongArrayRegion;
    _jni->SetFloatArrayRegion = SetFloatArrayRegion;
    _jni->SetDoubleArrayRegion = SetDoubleArrayRegion;
    _jni->RegisterNatives = RegisterNatives;
    _jni->UnregisterNatives = UnregisterNatives;
    _jni->MonitorEnter = MonitorEnter;
    _jni->MonitorExit = MonitorExit;
    _jni->GetJavaVM = GetJavaVM;
    _jni->GetStringRegion = GetStringRegion;
    _jni->GetStringUTFRegion = GetStringUTFRegion;
    _jni->GetPrimitiveArrayCritical = GetPrimitiveArrayCritical;
    _jni->ReleasePrimitiveArrayCritical = ReleasePrimitiveArrayCritical;
    _jni->GetStringCritical = GetStringCritical;
    _jni->ReleaseStringCritical = ReleaseStringCritical;
    _jni->NewWeakGlobalRef = NewWeakGlobalRef;
    _jni->DeleteWeakGlobalRef = DeleteWeakGlobalRef;
    _jni->ExceptionCheck = ExceptionCheck;
    _jni->NewDirectByteBuffer = NewDirectByteBuffer;
    _jni->GetDirectBufferAddress = GetDirectBufferAddress;
    _jni->GetDirectBufferCapacity = GetDirectBufferCapacity;
    _jni->GetObjectRefType = GetObjectRefType;

    jvm = _jvm;
    jni = _jni;
}
