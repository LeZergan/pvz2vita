/*
 * Minimal KHR/khrplatform.h for the PvZ2 Vita loader.
 *
 * The VitaSDK's EGL/egl.h #includes <KHR/khrplatform.h>, which is not shipped
 * with this SDK. This provides just the Khronos scalar typedefs EGL/GLES need,
 * backed by <stdint.h>. (Functional ABI typedefs only.)
 */
#ifndef __khrplatform_h_
#define __khrplatform_h_

#include <stdint.h>
#include <stddef.h>

#define KHRONOS_APICALL
#define KHRONOS_APIENTRY
#define KHRONOS_APIATTRIBUTES

typedef int32_t   khronos_int32_t;
typedef uint32_t  khronos_uint32_t;
typedef int64_t   khronos_int64_t;
typedef uint64_t  khronos_uint64_t;
#define KHRONOS_SUPPORT_INT64  1
#define KHRONOS_SUPPORT_FLOAT  1

typedef signed   char  khronos_int8_t;
typedef unsigned char  khronos_uint8_t;
typedef signed   short khronos_int16_t;
typedef unsigned short khronos_uint16_t;

typedef intptr_t  khronos_intptr_t;
typedef uintptr_t khronos_uintptr_t;
typedef ptrdiff_t khronos_ssize_t;
typedef size_t    khronos_usize_t;

typedef float     khronos_float_t;

typedef khronos_uint64_t khronos_utime_nanoseconds_t;
typedef khronos_int64_t  khronos_stime_nanoseconds_t;

typedef enum {
    KHRONOS_FALSE = 0,
    KHRONOS_TRUE  = 1,
    KHRONOS_BOOLEAN_ENUM_FORCE_SIZE = 0x7FFFFFFF
} khronos_boolean_enum_t;

#endif /* __khrplatform_h_ */
