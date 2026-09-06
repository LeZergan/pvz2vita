/*
 * FalsoJNI_ImplBridge.c
 *
 * Fake Java Native Interface, providing JavaVM and JNIEnv objects.
 *
 * Copyright (C) 2021 Andy Nguyen
 * Copyright (C) 2021 Rinnegatamante
 * Copyright (C) 2022 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "FalsoJNI_Impl.h"
#include "FalsoJNI_Logger.h"

#include "FalsoJNI_ImplBridge.h"
#include "FalsoJNI.h"

#include <string.h>
#include <malloc.h>
#include <pthread.h>

#include "converter.h"

jfieldID getFieldIdByName(const char* name) {
    for (int i = 0; i < nameToFieldId_size() / sizeof(NameToFieldID); i++) {
        if (strcmp(name, nameToFieldId[i].name) == 0) {
            return (jfieldID) nameToFieldId[i].id;
        }
    }

    fjni_logv_warn("Unknown field name \"%s\"", name);
    return NULL;
}

const char* fieldTypeToStr(FIELD_TYPE t) {
    switch (t) {
        case FIELD_TYPE_INT:
            return "FIELD_TYPE_INT";
        case FIELD_TYPE_OBJECT:
            return "FIELD_TYPE_OBJECT";
        case FIELD_TYPE_BOOLEAN:
            return "FIELD_TYPE_BOOLEAN";
        case FIELD_TYPE_BYTE:
            return "FIELD_TYPE_BYTE";
        case FIELD_TYPE_CHAR:
            return "FIELD_TYPE_CHAR";
        case FIELD_TYPE_SHORT:
            return "FIELD_TYPE_SHORT";
        case FIELD_TYPE_LONG:
            return "FIELD_TYPE_LONG";
        case FIELD_TYPE_FLOAT:
            return "FIELD_TYPE_FLOAT";
        case FIELD_TYPE_DOUBLE:
            return "FIELD_TYPE_DOUBLE";
        default:
            return "FIELD_TYPE_UNKNOWN";
    }
}

jsize getFieldTypeSize(FIELD_TYPE fieldType) {
    switch (fieldType) {
        case FIELD_TYPE_OBJECT:
            return sizeof(jobject);
        case FIELD_TYPE_BOOLEAN:
            return sizeof(jboolean);
        case FIELD_TYPE_BYTE:
            return sizeof(jbyte);
        case FIELD_TYPE_CHAR:
            return sizeof(jchar);
        case FIELD_TYPE_SHORT:
            return sizeof(jshort);
        case FIELD_TYPE_INT:
            return sizeof(jint);
        case FIELD_TYPE_LONG:
            return sizeof(jlong);
        case FIELD_TYPE_FLOAT:
            return sizeof(jfloat);
        case FIELD_TYPE_DOUBLE:
            return sizeof(jdouble);
        default:
            return sizeof(void *);
    }
}

jobject getObjectFieldValueById(jfieldID id) {
    getFieldValueById(jobject, FIELD_TYPE_OBJECT, FieldsObject, fieldsObject, fieldsObject_size, id, (jobject)0x42424242);
}

jint getIntFieldValueById(jfieldID id) {
    getFieldValueById(jint, FIELD_TYPE_INT, FieldsInt, fieldsInt, fieldsInt_size, id, 1);
}

jboolean getBooleanFieldValueById(jfieldID id) {
    getFieldValueById(jboolean, FIELD_TYPE_BOOLEAN, FieldsBoolean, fieldsBoolean, fieldsBoolean_size, id, JNI_FALSE);
}

jbyte getByteFieldValueById(jfieldID id) {
    getFieldValueById(jbyte, FIELD_TYPE_BYTE, FieldsByte, fieldsByte, fieldsByte_size, id, 'a');
}

jchar getCharFieldValueById(jfieldID id) {
    getFieldValueById(jchar, FIELD_TYPE_CHAR, FieldsChar, fieldsChar, fieldsChar_size, id, 'b');
}

jshort getShortFieldValueById(jfieldID id) {
    getFieldValueById(jshort, FIELD_TYPE_SHORT, FieldsShort, fieldsShort, fieldsShort_size, id, 1);
}

jlong getLongFieldValueById(jfieldID id) {
    getFieldValueById(jlong, FIELD_TYPE_LONG, FieldsLong, fieldsLong, fieldsLong_size, id, 1);
}

jfloat getFloatFieldValueById(jfieldID id) {
    getFieldValueById(jfloat, FIELD_TYPE_FLOAT, FieldsFloat, fieldsFloat, fieldsFloat_size, id, 1.0f);
}

jdouble getDoubleFieldValueById(jfieldID id) {
    getFieldValueById(jdouble, FIELD_TYPE_DOUBLE, FieldsDouble, fieldsDouble, fieldsDouble_size, id, 1);
}

void setObjectFieldValueById(jfieldID id, jobject value) {
    setFieldValueById(jobject, FIELD_TYPE_OBJECT, FieldsObject, fieldsObject, fieldsObject_size, id, value);
}

void setIntFieldValueById(jfieldID id, jint value) {
    setFieldValueById(jint, FIELD_TYPE_INT, FieldsInt, fieldsInt, fieldsInt_size, id, value);
}

void setBooleanFieldValueById(jfieldID id, jboolean value) {
    setFieldValueById(jboolean, FIELD_TYPE_BOOLEAN, FieldsBoolean, fieldsBoolean, fieldsBoolean_size, id, value);
}

void setByteFieldValueById(jfieldID id, jbyte value) {
    setFieldValueById(jbyte, FIELD_TYPE_BYTE, FieldsByte, fieldsByte, fieldsByte_size, id, value);
}

void setCharFieldValueById(jfieldID id, jchar value) {
    setFieldValueById(jchar, FIELD_TYPE_CHAR, FieldsChar, fieldsChar, fieldsChar_size, id, value);
}

void setShortFieldValueById(jfieldID id, jshort value) {
    setFieldValueById(jshort, FIELD_TYPE_SHORT, FieldsShort, fieldsShort, fieldsShort_size, id, value);
}

void setLongFieldValueById(jfieldID id, jlong value) {
    setFieldValueById(jlong, FIELD_TYPE_LONG, FieldsLong, fieldsLong, fieldsLong_size, id, value);
}

void setFloatFieldValueById(jfieldID id, jfloat value) {
    setFieldValueById(jfloat, FIELD_TYPE_FLOAT, FieldsFloat, fieldsFloat, fieldsFloat_size, id, value);
}

void setDoubleFieldValueById(jfieldID id, jdouble value) {
    setFieldValueById(jdouble, FIELD_TYPE_DOUBLE, FieldsDouble, fieldsDouble, fieldsDouble_size, id, value);
}

jmethodID getMethodIdByName(const char* name) {
    for (int i = 0; i < nameToMethodId_size() / sizeof(NameToMethodID); i++) {
        if (strcmp(name, nameToMethodId[i].name) == 0) {
            return (jmethodID) nameToMethodId[i].id;
        }
    }
    return NULL;
}

jobject methodObjectCall(jmethodID id, va_list args) {
    for (int i = 0; i < methodsObject_size() / sizeof(MethodsObject); i++) {
        if (methodsObject[i].id == (int)id) {
            return methodsObject[i].Method(id, args);
        }
    }

    /* Stubbed (unknown) object method, typed by return signature:
     *  - String-returning  -> empty string  ("" is "unset"; NULL would crash
     *    engine code that does strlen()/String ops on the result).
     *  - other object types -> NULL  (e.g. GetActivity()Landroid/app/Activity;)
     *    so the engine's null-check skips it instead of dereferencing an
     *    empty String it wrongly took for a real object. */
    if (id == FJNI_STUB_METHOD_STR) {
        return jni->NewStringUTF(&jni, "");
    }
    if (id == FJNI_STUB_METHOD_OBJ) {
        return NULL;
    }
    { char t = 'L'; const char* nm = fjni_namestub_name(id, &t);
      if (nm) {
        static int n = 0; int chatty = (n++ < 120 || (n & 0x3ff) == 0);
        if (t == 'S') { if (chatty) fjni_logv_warn("[OBJPOLL] %s() -> \"\" (#%d)", nm, n);
                        return jni->NewStringUTF(&jni, ""); }
        if (chatty) fjni_logv_warn("[OBJPOLL] %s() -> NULL (#%d)", nm, n);
        return NULL;
      } }

    fjni_logv_warn("method ID %i not found!", (int)id);
    return NULL;
}

void methodVoidCall(jmethodID id, va_list args) {
    for (int i = 0; i < methodsVoid_size() / sizeof(MethodsVoid); i++) {
        if (methodsVoid[i].id == (int)id) {
            return methodsVoid[i].Method(id, args);
        }
    }

    { const char* nm = fjni_namestub_name(id, NULL);
      if (nm) {
        static int n = 0; if (n++ < 120 || (n & 0x3ff) == 0)
            fjni_logv_warn("[VOIDPOLL] %s() (#%d)", nm, n);
        return;
      } }

    fjni_logv_warn("method ID %i not found!", (int)id);
}

/* Name-stub registry: unique IDs + names + return-type tag so ANY polled/called
 * unknown method (not just booleans) is identifiable in the log instead of
 * collapsing to the shared 0x7FFFFFFx stub id. type tag: 'Z' bool, 'I' int,
 * 'S' String, 'L' object, 'V' void. */
static char g_namestub_names[1024][80];
static char g_namestub_types[1024];
static int  g_namestub_count = 0;
jmethodID fjni_register_namestub(const char* name, char type) {
    for (int i = 0; i < g_namestub_count; i++)
        if (strcmp(g_namestub_names[i], name) == 0)
            return (jmethodID)(FJNI_STUB_BOOL_BASE + i);
    if (g_namestub_count < 1024) {
        snprintf(g_namestub_names[g_namestub_count], 80, "%s", name);
        g_namestub_types[g_namestub_count] = type;
        return (jmethodID)(FJNI_STUB_BOOL_BASE + g_namestub_count++);
    }
    return (jmethodID)FJNI_STUB_METHOD_ID;
}
const char* fjni_namestub_name(jmethodID id, char* type_out) {
    int idx = (int)id - FJNI_STUB_BOOL_BASE;
    if (idx >= 0 && idx < g_namestub_count) {
        if (type_out) *type_out = g_namestub_types[idx];
        return g_namestub_names[idx];
    }
    return NULL;
}
/* Back-compat wrappers (boolean path unchanged for callers). */
jmethodID fjni_register_boolstub(const char* name) { return fjni_register_namestub(name, 'Z'); }
const char* fjni_boolstub_name(jmethodID id) { return fjni_namestub_name(id, NULL); }

jboolean methodBooleanCall(jmethodID id, va_list args) {
    for (int i = 0; i < methodsBoolean_size() / sizeof(MethodsBoolean); i++) {
        if (methodsBoolean[i].id == (int)id) {
            return methodsBoolean[i].Method(id, args);
        }
    }

    const char* bn = fjni_boolstub_name(id);
    if (bn) {
        /* ★ ONLINE SPOOF (van-gsm9): report connectivity/online/auth/signed-in polls as TRUE so
         * the game believes it is fully online and never stalls waiting for a connection. */
        extern int g_pvz2_online_spoof;
        if (g_pvz2_online_spoof &&
            (strstr(bn, "Connect") || strstr(bn, "connect") || strstr(bn, "Online") ||
             strstr(bn, "online") || strstr(bn, "Network") || strstr(bn, "network") ||
             strstr(bn, "SignedIn") || strstr(bn, "signedIn") || strstr(bn, "Reachable") ||
             strstr(bn, "Internet") || strstr(bn, "internet") || strstr(bn, "LoggedIn") ||
             strstr(bn, "loggedIn") || strstr(bn, "Authenticated") || strstr(bn, "Auth") ||
             strstr(bn, "IsLogged") || strstr(bn, "HasNetwork"))) {
            static int y = 0;
            if (y++ < 60) fjni_logv_warn("[BOOLPOLL] %s() -> TRUE (online spoof)", bn);
            return JNI_TRUE;
        }
        static int n = 0;
        if (n++ < 80 || (n & 0x3ff) == 0)
            fjni_logv_warn("[BOOLPOLL] %s() -> false (#%d)", bn, n);
        return JNI_FALSE;
    }
    fjni_logv_warn("method ID %i not found!", (int)id);
    return JNI_FALSE;
}

jbyte methodByteCall(jmethodID id, va_list args) {
    for (int i = 0; i < methodsByte_size() / sizeof(MethodsByte); i++) {
        if (methodsByte[i].id == (int)id) {
            return methodsByte[i].Method(id, args);
        }
    }

    fjni_logv_warn("method ID %i not found!", (int)id);
    return 0;
}

jshort methodShortCall(jmethodID id, va_list args) {
    for (int i = 0; i < methodsShort_size() / sizeof(MethodsShort); i++) {
        if (methodsShort[i].id == (int)id) {
            return methodsShort[i].Method(id, args);
        }
    }

    fjni_logv_warn("method ID %i not found!", (int)id);
    return 0;
}

jdouble methodDoubleCall(jmethodID id, va_list args) {
    for (int i = 0; i < methodsDouble_size() / sizeof(MethodsDouble); i++) {
        if (methodsDouble[i].id == (int)id) {
            return methodsDouble[i].Method(id, args);
        }
    }

    { const char* nm = fjni_namestub_name(id, NULL);
      if (nm) {
        static int n = 0; if (n++ < 120 || (n & 0x3ff) == 0)
            fjni_logv_warn("[DBLPOLL] %s() -> 0.0 (#%d)", nm, n);
        return 0;
      } }

    fjni_logv_warn("method ID %i not found!", (int)id);
    return 0;
}

jchar methodCharCall(jmethodID id, va_list args) {
    for (int i = 0; i < methodsChar_size() / sizeof(MethodsChar); i++) {
        if (methodsChar[i].id == (int)id) {
            return methodsChar[i].Method(id, args);
        }
    }

    fjni_logv_warn("method ID %i not found!", (int)id);
    return 0;
}

jlong methodLongCall(jmethodID id, va_list args) {
    for (int i = 0; i < methodsLong_size() / sizeof(MethodsLong); i++) {
        if (methodsLong[i].id == (int)id) {
            return methodsLong[i].Method(id, args);
        }
    }

    { const char* nm = fjni_namestub_name(id, NULL);
      if (nm) {
        static int n = 0; if (n++ < 120 || (n & 0x3ff) == 0)
            fjni_logv_warn("[LONGPOLL] %s() -> 0 (#%d)", nm, n);
        return 0;
      } }

    fjni_logv_warn("method ID %i not found!", (int)id);
    return -1;
}

jint methodIntCall(jmethodID id, va_list args) {
    for (int i = 0; i < methodsInt_size() / sizeof(MethodsInt); i++) {
        if (methodsInt[i].id == (int)id) {
            return methodsInt[i].Method(id, args);
        }
    }

    { const char* nm = fjni_namestub_name(id, NULL);
      if (nm) {
        /* ★ online spoof: HTTP status/response-code getters -> 200 so the framework's HTTP
         * transactions read as successful (they fire success but read the code separately). */
        extern int g_pvz2_http_txn_ok;   /* only when HTTP-txn success is on (else Error + 200 = inconsistent -> crash) */
        if (g_pvz2_http_txn_ok &&
            (strstr(nm, "ResponseCode") || strstr(nm, "StatusCode") || strstr(nm, "responseCode") ||
             strstr(nm, "statusCode") || strstr(nm, "HttpStatus") || strstr(nm, "HttpCode"))) {
            static int y = 0; if (y++ < 30) fjni_logv_warn("[INTPOLL] %s() -> 200 (spoof)", nm);
            return 200;
        }
        static int n = 0; if (n++ < 120 || (n & 0x3ff) == 0)
            fjni_logv_warn("[INTPOLL] %s() -> 0 (#%d)", nm, n);
        return 0;   /* 0 not -1: an unknown int getter returning -1 poisoned dataversion to -1.-1.-1 */
      } }

    fjni_logv_warn("method ID %i not found!", (int)id);
    return -1;
}

jfloat methodFloatCall(jmethodID id, va_list args) {
    for (int i = 0; i < methodsFloat_size() / sizeof(MethodsFloat); i++) {
        if (methodsFloat[i].id == (int)id) {
            return methodsFloat[i].Method(id, args);
        }
    }

    { const char* nm = fjni_namestub_name(id, NULL);
      if (nm) {
        static int n = 0; if (n++ < 120 || (n & 0x3ff) == 0)
            fjni_logv_warn("[FLTPOLL] %s() -> 0.0 (#%d)", nm, n);
        return 0.0f;
      } }

    fjni_logv_warn("method ID %i not found!", (int)id);
    return 96.0f;
}

JavaDynArray * jda_alloc(jsize len, FIELD_TYPE type) {
    if (len < 0) {
        return NULL;
    }

    void * array = malloc(len * getFieldTypeSize(type));
    if (!array) {
        return NULL;
    }

    JavaDynArray * ret = malloc(sizeof(JavaDynArray));
    if (!ret) {
        free(array);
        return NULL;
    }

    ret->magic = JDA_MAGIC;
    ret->array = array;
    ret->len = len;
    ret->type = type;

    return ret;
}

jsize jda_sizeof(JavaDynArray * jda) {
    if (!jda || jda->magic != JDA_MAGIC)
        return -1;

    return jda->len;
}

jboolean jda_realloc(JavaDynArray * jda, jsize len) {
    if (!jda || jda->magic != JDA_MAGIC || len < 0)
        return JNI_FALSE;

    void * res = realloc(jda->array, len * getFieldTypeSize(jda->type));
    if (res == NULL) {
        return JNI_FALSE;
    }

    jda->array = res;
    jda->len = len;

    return JNI_TRUE;
}

jboolean jda_free(JavaDynArray * jda) {
    if (!jda || jda->magic != JDA_MAGIC) return JNI_FALSE;

    free(jda->array);
    jda->magic = 0;
    free(jda);

    return JNI_TRUE;
}

jboolean jstr_utf16_to_utf8(JavaString * jstr) {
    if (!jstr) return JNI_FALSE;

    if (jstr->utf8 == NULL) {
        jstr->utf8 = jda_alloc(jstr->utf16->len+1, FIELD_TYPE_BYTE);
        if (jstr->utf8 == NULL) {
            return JNI_FALSE;
        }
    } else if (jstr->utf8->len < jstr->utf16->len+1) {
        if (jda_realloc(jstr->utf8, jstr->utf16->len+1) == JNI_FALSE) {
            return JNI_FALSE;
        }
    }

    utf16_to_utf8(jstr->utf16->array, jstr->utf16->len, jstr->utf8->array, jstr->utf8->len);

    char * arr = jstr->utf8->array;
    arr[jstr->utf8->len - 1] = '\0';
    return JNI_TRUE;
}

jboolean jstr_utf8_to_utf16(JavaString * jstr) {
    if (!jstr) return JNI_FALSE;

    if (jstr->utf8 == NULL) {
        return JNI_FALSE;
    }

    if (jstr->utf16->len + 1 < jstr->utf8->len) {
        if (jda_realloc(jstr->utf16, jstr->utf8->len - 1) == JNI_FALSE) {
            return JNI_FALSE;
        }
    }

    utf8_to_utf16(jstr->utf8->array, jstr->utf8->len - 1, jstr->utf16->array, jstr->utf16->len);

    return JNI_TRUE;
}

va_list _AtoV(int dummy, ...) {
    va_list args1;
    va_start(args1, dummy);
    va_list args2;
    va_copy(args2, args1);
    va_end(args1);
    return args2;
}
