#ifndef __khrplatform_h_
#define __khrplatform_h_

/*
 * Copyright (c) 2008-2018 The Khronos Group Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and/or associated documentation files (the
 * "Materials"), to deal in the Materials without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Materials, and to
 * permit persons to whom the Materials are furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Materials.
 *
 * THE MATERIALS ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * MATERIALS OR THE USE OR OTHER DEALINGS IN THE MATERIALS.
 */

/*
 * Simplified khrplatform.h for PS Vita (ARM, GCC).
 *
 * The VitaSDK ships GLES2/gl2platform.h and EGL/eglplatform.h which
 * both #include <KHR/khrplatform.h>, but the KHR directory is missing.
 * This file provides the types and macros those headers need.
 */

/* Platform-specific types and definitions.
 * Defaults taken from the Khronos reference header, adjusted for
 * 32-bit ARM bare-metal embedded targets.
 */

/*-------------------------------------------------------------------------
 * Definition of KHRONOS_APICALL
 *-------------------------------------------------------------------------
 * This precedes the return type of the function in the function prototype.
 */
#if defined(_WIN32) && !defined(__SCITECH_SNAP__)
#   define KHRONOS_APICALL __declspec(dllimport)
#elif defined(__SYMBIAN32__)
#   define KHRONOS_APICALL IMPORT_C
#elif defined(__ANDROID__)
#   define KHRONOS_APICALL __attribute__((visibility("default")))
#else
#   define KHRONOS_APICALL
#endif

/*-------------------------------------------------------------------------
 * Definition of KHRONOS_APIENTRY
 *-------------------------------------------------------------------------
 * This follows the return type of the function and precedes the function
 * name in the function prototype.
 */
#if defined(_WIN32) && !defined(_WIN32_WCE) && !defined(__SCITECH_SNAP__)
    /* Win32 but not WinCE */
#   define KHRONOS_APIENTRY __stdcall
#else
#   define KHRONOS_APIENTRY
#endif

/*-------------------------------------------------------------------------
 * Definition of KHRONOS_APIATTRIBUTES
 *-------------------------------------------------------------------------
 */
#if defined(__arm) && defined(__ARM_ARCH_7A__)
#   define KHRONOS_APIATTRIBUTES
#else
#   define KHRONOS_APIATTRIBUTES
#endif

/*-------------------------------------------------------------------------
 * basic type definitions
 *-----------------------------------------------------------------------*/
#if (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L) || \
    defined(__GNUC__) || defined(__SCO__) || defined(__USLC__)

/*
 * Using <stdint.h>
 */
#include <stdint.h>
typedef int32_t                 khronos_int32_t;
typedef uint32_t                khronos_uint32_t;
typedef int64_t                 khronos_int64_t;
typedef uint64_t                khronos_uint64_t;

#elif defined(__VMS) || defined(__sgi)

/*
 * Using <inttypes.h>
 */
#include <inttypes.h>
typedef int32_t                 khronos_int32_t;
typedef uint32_t                khronos_uint32_t;
typedef int64_t                 khronos_int64_t;
typedef uint64_t                khronos_uint64_t;

#elif defined(_WIN32) && !defined(__SCITECH_SNAP__)

/*
 * Win32
 */
typedef __int32                 khronos_int32_t;
typedef unsigned __int32        khronos_uint32_t;
typedef __int64                 khronos_int64_t;
typedef unsigned __int64        khronos_uint64_t;

#else

/*
 * Fallback
 */
#include <stdint.h>
typedef int32_t                 khronos_int32_t;
typedef uint32_t                khronos_uint32_t;
typedef int64_t                 khronos_int64_t;
typedef uint64_t                khronos_uint64_t;

#endif

/*
 * Types that are (so far) the same on all platforms
 */
typedef signed   char  khronos_int8_t;
typedef unsigned char  khronos_uint8_t;
typedef signed   short int khronos_int16_t;
typedef unsigned short int khronos_uint16_t;
typedef float           khronos_float_t;

/*
 * Types that differ between LLP64 and LP64 architectures - note: not
 * used on 32-bit ARM.
 */
#ifdef _WIN64
typedef signed   long long int khronos_intptr_t;
typedef unsigned long long int khronos_uintptr_t;
typedef signed   long long int khronos_ssize_t;
typedef unsigned long long int khronos_usize_t;
#else
typedef signed   long  int     khronos_intptr_t;
typedef unsigned long  int     khronos_uintptr_t;
typedef signed   long  int     khronos_ssize_t;
typedef unsigned long  int     khronos_usize_t;
#endif

/*
 * Definition of KHRONOS_SUPPORT_INT64 - enable for targets
 * where 64-bit atomics are needed.
 */
#ifndef KHRONOS_SUPPORT_INT64
#define KHRONOS_SUPPORT_INT64 1
#endif

#if KHRONOS_SUPPORT_INT64
/* Time value, for 64-bit signed integers */
typedef khronos_int64_t        khronos_stime_nanoseconds_t;
/* Unsigned time value, for 64-bit unsigned integers */
typedef khronos_uint64_t       khronos_utime_nanoseconds_t;
#endif

/*
 * Definition of KHRONOS_SUPPORT_FLOAT - enable for targets
 * supporting float math.
 */
#ifndef KHRONOS_SUPPORT_FLOAT
#define KHRONOS_SUPPORT_FLOAT 1
#endif

/*
 * Boolean type
 */
typedef khronos_uint8_t khronos_bool_t;

#define KHRONOS_TRUE  ((khronos_bool_t)1)
#define KHRONOS_FALSE ((khronos_bool_t)0)

/*
 * Helper for function pointer typedefs
 */
#define KHRONOS_APIENTRYP KHRONOS_APIENTRY *

#endif /* __khrplatform_h_ */
