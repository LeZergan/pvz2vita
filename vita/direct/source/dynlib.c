/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2021      Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 * Copyright (C) 2026      Ellie J Turner
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  dynlib.c
 * @brief Resolving dynamic imports of the .so.
 */

#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <inttypes.h>
#include <malloc.h>
#include <math.h>
#include <netdb.h>
#include <pwd.h>
#include <string.h>
#include <setjmp.h>
#include <semaphore.h>
#include <wchar.h>
#include <wctype.h>
#include <locale.h>
#include <poll.h>
#include <arpa/inet.h>

#include <sys/stat.h>
#include <sys/unistd.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <so_util/so_util.h>
#include <utime.h>

#include "utils/glutil.h"
#include "reimpl/math_softfp.h"
#include "utils/utils.h"
#ifdef USE_PVR_PSP2
#include "utils/pvr_init.h"
#endif
#include "utils/logger.h"
#include "utils/telemetry.h"

#ifdef USE_SCELIBC_IO
#include <libc_bridge/libc_bridge.h>
#endif

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#include "reimpl/errno.h"
#include "reimpl/io.h"
#include "reimpl/log.h"
#include "reimpl/mem.h"
#include "reimpl/miniz_zlib.h"
#include "reimpl/pthr.h"
#include "reimpl/sys.h"
#include "reimpl/egl.h"
#ifdef USE_PVR_PSP2
#include "reimpl/egl_pvr_shim.h"
#endif
#include "reimpl/time64.h"
#include "reimpl/bionic_time.h"
#include "reimpl/asset_manager.h"

const unsigned int __page_size = PAGE_SIZE;

extern void * _ZNSt9exceptionD2Ev;
extern void * _ZSt17__throw_bad_allocv;
extern void * _ZSt9terminatev;
extern void * _ZdaPv;
extern void * _ZdlPv;
extern void * _Znaj;
extern void * __cxa_allocate_exception;
extern void * __cxa_begin_catch;
extern void * __cxa_end_catch;
extern void * __cxa_free_exception;
extern void * __cxa_rethrow;
extern void * __cxa_throw;
extern void * __gxx_personality_v0;
extern void *_ZNSt8bad_castD1Ev;
extern void *_ZTISt8bad_cast;
extern void *_ZTISt9exception;
extern void *_ZTVN10__cxxabiv117__class_type_infoE;
extern void *_ZTVN10__cxxabiv120__si_class_type_infoE;
extern void *_ZTVN10__cxxabiv121__vmi_class_type_infoE;
extern void *_Znwj;
extern void *__aeabi_atexit;
extern void *__aeabi_d2lz;
extern void *__aeabi_d2ulz;
extern void *__aeabi_dadd;
extern void *__aeabi_dcmpgt;
extern void *__aeabi_dcmplt;
extern void *__aeabi_ddiv;
extern void *__aeabi_dmul;
extern void *__aeabi_f2lz;
extern void *__aeabi_f2ulz;
extern void *__aeabi_i2d;
extern void *__aeabi_idiv;
extern void *__aeabi_idivmod;
extern void *__aeabi_l2d;
extern void *__aeabi_l2f;
extern void *__aeabi_ldivmod;
extern void *__aeabi_memclr;
extern void *__aeabi_memcpy;
extern void *__aeabi_memmove;
extern void *__aeabi_memset4;
extern void *__aeabi_memset8;
extern void *__aeabi_memset;
extern void *__aeabi_ui2d;
extern void *__aeabi_uidiv;
extern void *__aeabi_uidivmod;
extern void *__aeabi_ul2d;
extern void *__aeabi_ul2f;
extern void *__aeabi_uldivmod;
extern void *__aeabi_unwind_cpp_pr0;
extern void *__aeabi_unwind_cpp_pr1;
extern void *__cxa_atexit;
extern void *__cxa_call_unexpected;
extern void *__cxa_finalize;
extern void *__cxa_guard_acquire;
extern void *__cxa_guard_release;
extern void *__cxa_pure_virtual;
extern void *__gnu_ldivmod_helper;
extern void *__gnu_unwind_frame;
/* Real ARM exidx locator (defined in main.c, module-aware) — enables C++
 * exception catching across libPVZ2/libc++. */
extern uintptr_t __gnu_Unwind_Find_exidx(uintptr_t pc, int *pcount);
extern void *__srget;
extern void *__stack_chk_guard;
extern void *__swbuf;

extern const char *BIONIC_ctype_;
extern const short *BIONIC_tolower_tab_;
extern const short *BIONIC_toupper_tab_;

/* PvZ2 modules (defined in main.c). libc++_shared provides the __ndk1 C++
 * runtime; libNimble the EA bridge; libPVZ2 the engine. */
extern so_module so_mod_libcpp;
extern so_module so_mod_nimble;
extern so_module so_mod_pvz2;

/* PvZ2 import gap (dynlib_pvz2_gap.c). */
extern so_default_dynlib pvz2_gap_dynlib[];
extern const int pvz2_gap_dynlib_count;

static FILE __sF_fake[3];

void *dlopen_soloader(const char *filename, int flags);
void *dlsym_soloader(void * handle, const char * symbol);
void *lookup_symbol_soloader_quiet(const char *symbol);

__attribute__((noreturn))
static void __assert2_soloader(const char *file, int line, const char *function, const char *expression) {
    l_fatal("__assert2: %s:%d (%s): %s",
            file ? file : "<unknown>",
            line,
            function ? function : "<unknown>",
            expression ? expression : "<null>");
    abort();
}

static int64_t lseek64_soloader(int fd, int64_t offset, int whence) {
    /* Observed: the game passes OBB seek offsets with the real 32-bit value in the
     * HIGH half (off = real<<32, low 32 == 0) -> seeks into garbage, the read fails,
     * and async resource loads hang forever (the "infinite loading" title stall). The
     * OBB is <4GB so every genuine offset fits in 32 bits (high half 0); the only value
     * with low32==0 and a non-zero high half is the shifted-garbage case -> recover the
     * real offset from the high half. Genuine offset 0 (low32==0, high==0) is untouched. */
    if (whence == SEEK_SET && ((uint64_t)offset & 0xffffffffULL) == 0) {
        uint64_t hi = (uint64_t)offset >> 32;
        if (hi != 0 && hi < 0x80000000ULL) {
            static int wn = 0;
            if (wn++ < 20) l_info("[OBBIO] lseek64 shifted-offset recovered: 0x%llx -> 0x%llx",
                                  (unsigned long long)offset, (unsigned long long)hi);
            offset = (int64_t)hi;
        }
    }
    if ((int64_t)(off_t)offset != offset) {
        errno = EOVERFLOW;
        return -1;
    }

    return (int64_t)lseek_soloader(fd, (off_t)offset, whence);
}

/* The game imports Android's 32-bit lseek (its off_t is a 32-bit long) alongside
 * 64-bit lseek64. The loader's own off_t is 64-bit, so mapping "lseek" straight to
 * the 64-bit lseek_soloader mismatches the AAPCS: the game passes the offset as a
 * single 32-bit arg in r1, but a 64-bit param is read 8-byte-aligned from r2:r3 --
 * picking up whence in r2 and a stale offset copy in r3, so the loader sees
 * off = real<<32 and seeks into garbage. The OBB read then fails and async resource
 * loads hang forever (the "infinite loading" title stall). Wrap "lseek" with a 32-bit
 * offset param so the arg is taken from r1 correctly, then widen to the real lseek. */
static int32_t lseek32_soloader(int fd, int32_t offset, int whence) {
    return (int32_t)lseek_soloader(fd, (off_t)offset, whence);
}

/* mmap ABI fix. The game imports Android's `mmap` (32-bit off_t BYTE offset) and the
 * raw `__mmap2` syscall wrapper (32-bit PAGE offset). The loader's mmap() takes a 64-bit
 * off_t, so binding either straight to it mismatched the AAPCS: the game's 32-bit 6th arg
 * (at [sp+4]) was read as a 64-bit off_t (at [sp+8]) -> the real OBB offset landed in the
 * high 32 bits (off = real<<32) -> the file-backed mmap seeks into garbage, the mapped
 * resource is wrong, and the async world-map load hangs (the "infinite loading" stall).
 * Also __mmap2 was mapped to mmap WITHOUT the page->byte (<<12) conversion. Fix both. */
extern void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offs);
static void *mmap_bytes_soloader(void *addr, size_t length, int prot, int flags, int fd, uint32_t byte_off) {
    return mmap(addr, length, prot, flags, fd, (off_t)byte_off);
}
static void *mmap2_pages_soloader(void *addr, size_t length, int prot, int flags, int fd, uint32_t pgoff) {
    return mmap(addr, length, prot, flags, fd, (off_t)pgoff << 12);
}

/* ★ THE "infinite loading" ROOT. PvZ2 reads its main.pak (the OBB) via stdio:
 * fopen/fseeko/fread. Android's off_t is 32-bit, but the loader's SDK off_t is 64-bit,
 * so binding the game's fseeko straight to the loader's fseeko mismatches the AAPCS: the
 * game passes the 32-bit offset in r1, but a 64-bit off_t param is read 8-byte-aligned
 * from r2:r3, picking up a stale offset copy -> the loader sees off = real<<32 and seeks
 * far past EOF -> fread returns short/garbage -> the async world-map load never completes
 * (the game sits on the title splash forever). Wrap fseeko/ftello with a 32-bit offset so
 * the arg is taken from r1 correctly. (dynlib.c already TODO-flagged this at the mapping.) */
static int fseeko32_soloader(FILE *stream, int32_t offset, int whence) {
    return fseeko(stream, (off_t)offset, whence);
}
static int32_t ftello32_soloader(FILE *stream) {
    return (int32_t)ftello(stream);
}

static char *mktemp_soloader(char *template_name) {
    if (!template_name) {
        errno = EINVAL;
        return NULL;
    }

    int fd = mkstemp(template_name);
    if (fd < 0) {
        return NULL;
    }

    close(fd);
    return template_name;
}

static int deflateInit_soloader_(mz_streamp stream, int level, const char *version, int stream_size) {
    (void)version;
    (void)stream_size;
    return deflateInit(stream, level);
}

static int deflateInit2_soloader_(mz_streamp stream, int level, int method, int window_bits, int mem_level, int strategy, const char *version, int stream_size) {
    (void)version;
    (void)stream_size;
    return deflateInit2(stream, level, method, window_bits, mem_level, strategy);
}

static int inflateInit_soloader_(mz_streamp stream, const char *version, int stream_size) {
    (void)version;
    (void)stream_size;
    return inflateInit(stream);
}

static int inflateInit2_soloader_(mz_streamp stream, int window_bits, const char *version, int stream_size) {
    (void)version;
    (void)stream_size;
    return inflateInit2(stream, window_bits);
}

static int inflateReset2_soloader(mz_streamp stream, int window_bits) {
    inflateEnd(stream);
    return inflateInit2(stream, window_bits);
}


static int gzclose_soloader(void *file) {
    (void)file;
    return Z_STREAM_ERROR;
}

static char *gzgets_soloader(void *file, char *buf, int len) {
    (void)file;
    if (buf && len > 0) {
        buf[0] = '\0';
    }
    return NULL;
}

static void *gzopen_soloader(const char *path, const char *mode) {
    (void)path;
    (void)mode;
    return NULL;
}

static const char *opensl_interface_name(SLInterfaceID iid) {
    if (iid == SL_IID_ENGINE) {
        return "ENGINE";
    }
    if (iid == SL_IID_ANDROIDSIMPLEBUFFERQUEUE) {
        return "ANDROIDSIMPLEBUFFERQUEUE";
    }
    if (iid == SL_IID_ANDROIDCONFIGURATION) {
        return "ANDROIDCONFIGURATION";
    }
    if (iid == SL_IID_BUFFERQUEUE) {
        return "BUFFERQUEUE";
    }
    if (iid == SL_IID_PLAY) {
        return "PLAY";
    }
    if (iid == SL_IID_RECORD) {
        return "RECORD";
    }
    if (iid == SL_IID_VOLUME) {
        return "VOLUME";
    }
    return "?";
}

extern SLresult slCreateEngine_soloader_opensl(SLObjectItf *pEngine, SLuint32 numOptions,
        const SLEngineOption *pEngineOptions, SLuint32 numInterfaces,
        const SLInterfaceID *pInterfaceIds, const SLboolean *pInterfaceRequired);

static SLresult slCreateEngine_soloader(SLObjectItf *pEngine,
                                        SLuint32 numOptions,
                                        const SLEngineOption *pEngineOptions,
                                        SLuint32 numInterfaces,
                                        const SLInterfaceID *pInterfaceIds,
                                        const SLboolean *pInterfaceRequired) {
    return slCreateEngine_soloader_opensl(pEngine, numOptions, pEngineOptions,
                                          numInterfaces, pInterfaceIds,
                                          pInterfaceRequired);
}

#ifndef USE_PVR_PSP2
/* vitaGL is a GLES2 SUBSET and does not implement these functions, but the
 * Telltale engine's GL dispatch references them. Define stubs so the vitaGL
 * build links. They are state queries / cleanup / validate — non-critical:
 * filter & wrap queries return the values we force everywhere (LINEAR /
 * CLAMP_TO_EDGE); detach/validate/blendcolor are harmless no-ops. */
void glBlendColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { (void)r; (void)g; (void)b; (void)a; }
void glDetachShader(GLuint program, GLuint shader) { (void)program; (void)shader; }
void glValidateProgram(GLuint program) { (void)program; }
GLboolean glIsBuffer(GLuint buffer) { return buffer != 0u ? 1 : 0; }
void glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params) { if (params) glTexParameteri(target, pname, (GLint)params[0]); }
void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params) { (void)target; (void)pname; if (params) *params = 0.0f; }
void glGetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *params) { (void)target; (void)pname; if (params) *params = 0; }
void glGetTexParameteriv(GLenum target, GLenum pname, GLint *params) {
    (void)target;
    if (!params) { return; }
    switch (pname) {
        case 0x2800: case 0x2801: *params = 0x2601; break;       /* MIN/MAG_FILTER -> LINEAR */
        case 0x2802: case 0x2803: *params = 0x812F; break;       /* WRAP_S/T -> CLAMP_TO_EDGE */
        default: *params = 0; break;
    }
}
#endif

so_default_dynlib default_dynlib[] = {
    /* Complete the math/runtime imports used by the exact 4.5.2 library. */
    { "acosh", (uintptr_t)&acosh_sf },
    { "asinh", (uintptr_t)&asinh_sf },
    { "atanh", (uintptr_t)&atanh_sf },
    { "cbrt", (uintptr_t)&cbrt_sf },
    { "cosh", (uintptr_t)&cosh_sf },
    { "erf", (uintptr_t)&erf_sf },
    { "erfc", (uintptr_t)&erfc_sf },
    { "expm1", (uintptr_t)&expm1_sf },
    { "hypot", (uintptr_t)&hypot_sf },
    { "lgamma", (uintptr_t)&lgamma_sf },
    { "llrint", (uintptr_t)&llrint_sf },
    { "log1p", (uintptr_t)&log1p_sf },
    { "logb", (uintptr_t)&logb_sf },
    { "nearbyint", (uintptr_t)&nearbyint_sf },
    { "nextafter", (uintptr_t)&nextafter_sf },
    { "nextafterf", (uintptr_t)&nextafterf_sf },
    { "remainder", (uintptr_t)&remainder_sf },
    { "remquo", (uintptr_t)&remquo_sf },
    { "scalbnl", (uintptr_t)&scalbnl_sf },
    { "tgamma", (uintptr_t)&tgamma_sf },
    { "__fpclassifyd", (uintptr_t)&fpclassifyd_sf },
    { "__isfinite", (uintptr_t)&isfinite_sf },
    { "__signbit", (uintptr_t)&signbit_sf },
    { "__signbitf", (uintptr_t)&signbitf_sf },
    { "isnan", (uintptr_t)&isnan_sf },
    { "fputwc", (uintptr_t)&fputwc },
    { "inet_addr", (uintptr_t)&inet_addr },
        // Common C/C++ internals
        { "_ZNSt8bad_castD1Ev", (uintptr_t)&_ZNSt8bad_castD1Ev },
        { "_ZNSt9exceptionD2Ev", (uintptr_t)&_ZNSt9exceptionD2Ev },
        { "_ZSt9terminatev", (uintptr_t)&_ZSt9terminatev },
        { "_ZTISt8bad_cast", (uintptr_t)&_ZTISt8bad_cast },
        { "_ZTISt9exception", (uintptr_t)&_ZTISt9exception },
        { "_ZTVN10__cxxabiv117__class_type_infoE", (uintptr_t)&_ZTVN10__cxxabiv117__class_type_infoE },
        { "_ZTVN10__cxxabiv120__si_class_type_infoE", (uintptr_t)&_ZTVN10__cxxabiv120__si_class_type_infoE },
        { "_ZTVN10__cxxabiv121__vmi_class_type_infoE", (uintptr_t)&_ZTVN10__cxxabiv121__vmi_class_type_infoE },
        { "__aeabi_atexit", (uintptr_t)&__aeabi_atexit },
        { "__aeabi_d2lz", (uintptr_t)&__aeabi_d2lz },
        { "__aeabi_d2ulz", (uintptr_t)&__aeabi_d2ulz },
        { "__aeabi_dadd", (uintptr_t)&__aeabi_dadd },
        { "__aeabi_dcmpgt", (uintptr_t)&__aeabi_dcmpgt },
        { "__aeabi_dcmplt", (uintptr_t)&__aeabi_dcmplt },
        { "__aeabi_ddiv", (uintptr_t)&__aeabi_ddiv },
        { "__aeabi_dmul", (uintptr_t)&__aeabi_dmul },
        { "__aeabi_f2lz", (uintptr_t)&__aeabi_f2lz },
        { "__aeabi_f2ulz", (uintptr_t)&__aeabi_f2ulz },
        { "__aeabi_i2d", (uintptr_t)&__aeabi_i2d },
        { "__aeabi_idiv", (uintptr_t)&__aeabi_idiv },
        { "__aeabi_idivmod", (uintptr_t)&__aeabi_idivmod },
        { "__aeabi_l2d", (uintptr_t)&__aeabi_l2d },
        { "__aeabi_l2f", (uintptr_t)&__aeabi_l2f },
        { "__aeabi_ldivmod", (uintptr_t)&__aeabi_ldivmod },
        { "__aeabi_memclr", (uintptr_t)&aeabi_memclr_guarded },   /* [PvZ2 MEMGUARD] */
        { "__aeabi_memclr4", (uintptr_t)&aeabi_memclr_guarded },
        { "__aeabi_memclr8", (uintptr_t)&aeabi_memclr_guarded },
        { "__aeabi_memcpy", (uintptr_t)&memcpy_guarded },   /* [PvZ2 MEMGUARD] */
        { "__aeabi_memcpy4", (uintptr_t)&memcpy_guarded },
        { "__aeabi_memcpy8", (uintptr_t)&memcpy_guarded },
        { "__aeabi_memmove", (uintptr_t)&memmove_guarded },   /* [PvZ2 MEMGUARD] */
        { "__aeabi_memmove4", (uintptr_t)&memmove_guarded },
        { "__aeabi_memmove8", (uintptr_t)&memmove_guarded },
        { "__aeabi_memset", (uintptr_t)&aeabi_memset_guarded },   /* [PvZ2 MEMGUARD] swapped args */
        { "__aeabi_memset4",  (uintptr_t)&aeabi_memset_guarded },   /* [PvZ2 MEMGUARD] */
        { "__aeabi_memset8", (uintptr_t)&aeabi_memset_guarded },
        { "__aeabi_ui2d", (uintptr_t)&__aeabi_ui2d },
        { "__aeabi_uidiv", (uintptr_t)&__aeabi_uidiv },
        { "__aeabi_uidivmod", (uintptr_t)&__aeabi_uidivmod },
        { "__aeabi_ul2d", (uintptr_t)&__aeabi_ul2d },
        { "__aeabi_ul2f", (uintptr_t)&__aeabi_ul2f },
        { "__aeabi_uldivmod", (uintptr_t)&__aeabi_uldivmod },
        { "__aeabi_unwind_cpp_pr0", (uintptr_t)&__aeabi_unwind_cpp_pr0 },
        { "__aeabi_unwind_cpp_pr1", (uintptr_t)&__aeabi_unwind_cpp_pr1 },
        { "__atomic_cmpxchg", (uintptr_t)&__atomic_cmpxchg },
        { "__atomic_dec", (uintptr_t)&__atomic_dec },
        { "__atomic_inc", (uintptr_t)&__atomic_inc },
        { "__atomic_swap", (uintptr_t)&__atomic_swap },
        { "__cxa_atexit", (uintptr_t)&__cxa_atexit },
        { "__cxa_begin_cleanup", (uintptr_t)&ret0 },
        { "__cxa_finalize", (uintptr_t)&__cxa_finalize },
        { "__cxa_type_match", (uintptr_t)&ret0 },
        { "__gnu_Unwind_Find_exidx", (uintptr_t)&__gnu_Unwind_Find_exidx },
        { "__gnu_ldivmod_helper", (uintptr_t)&__gnu_ldivmod_helper },
        { "__gnu_unwind_frame", (uintptr_t)&__gnu_unwind_frame },
        { "__google_potentially_blocking_region_begin", (uintptr_t)&ret0 },
        { "__google_potentially_blocking_region_end", (uintptr_t)&ret0 },
        { "__gxx_personality_v0", (uintptr_t)&__gxx_personality_v0 },
        { "__isinf", (uintptr_t)&__isinf_soloader },
        { "__page_size", (uintptr_t)&__page_size },
        { "__sF", (uintptr_t)&__sF_fake },
        { "__srget", (uintptr_t)&__srget },
        { "__stack_chk_fail", (uintptr_t)&__stack_chk_fail_soloader },
        { "__stack_chk_guard", (uintptr_t)&__stack_chk_guard },
        { "__swbuf", (uintptr_t)&__swbuf },
        { "__system_property_get", (uintptr_t)&__system_property_get_soloader },
        { "__assert2", (uintptr_t)&__assert2_soloader },
        { "dl_unwind_find_exidx", (uintptr_t)&ret0 }, // TODO: stub/impl


        // ctype
        { "_ctype_", (uintptr_t)&BIONIC_ctype_ },
        { "_tolower_tab_", (uintptr_t)&BIONIC_tolower_tab_ },
        { "_toupper_tab_", (uintptr_t)&BIONIC_toupper_tab_ },
        { "isalnum", (uintptr_t)&isalnum },
        { "isalpha", (uintptr_t)&isalpha },
        { "isblank", (uintptr_t)&isblank },
        { "iscntrl", (uintptr_t)&iscntrl },
        { "isgraph", (uintptr_t)&isgraph },
        { "islower", (uintptr_t)&islower },
        { "isprint", (uintptr_t)&isprint },
        { "ispunct", (uintptr_t)&ispunct },
        { "isspace", (uintptr_t)&isspace },
        { "isupper", (uintptr_t)&isupper },
        { "isxdigit", (uintptr_t)&isxdigit },
        { "tolower", (uintptr_t)&tolower },
        { "toupper", (uintptr_t)&toupper },


        // Android SDK standard logging
        { "__android_log_assert", (uintptr_t)&__android_log_assert },
        { "__android_log_print", (uintptr_t)&__android_log_print },
        { "__android_log_vprint", (uintptr_t)&__android_log_vprint },
        { "__android_log_write", (uintptr_t)&__android_log_write },


        // AAssetManager
        { "AAsset_close", (uintptr_t)&AAsset_close },
        { "AAsset_getLength", (uintptr_t)&AAsset_getLength },
        { "AAsset_getRemainingLength", (uintptr_t)&AAsset_getRemainingLength },
        { "AAsset_read", (uintptr_t)&AAsset_read },
        { "AAsset_seek", (uintptr_t)&AAsset_seek },
        { "AAsset_openFileDescriptor", (uintptr_t)&AAsset_openFileDescriptor },
        { "AAssetDir_close", (uintptr_t)&AAssetDir_close },
        { "AAssetDir_getNextFileName", (uintptr_t)&AAssetDir_getNextFileName },
        { "AAssetManager_fromJava", (uintptr_t)&AAssetManager_fromJava },
        { "AAssetManager_open", (uintptr_t)&AAssetManager_open },
        { "AAssetManager_openDir", (uintptr_t)&AAssetManager_openDir },
        { "ANativeWindow_fromSurface", (uintptr_t)&ANativeWindow_fromSurface },
        { "ANativeWindow_release", (uintptr_t)&ANativeWindow_release },


        // Math
        { "acos", (uintptr_t)&acos_sf },
        { "acosf", (uintptr_t)&acosf_sf },
        { "asin", (uintptr_t)&asin_sf },
        { "asinf", (uintptr_t)&asinf_sf },
        { "atan", (uintptr_t)&atan_sf },
        { "atan2", (uintptr_t)&atan2_sf },
        { "atan2f", (uintptr_t)&atan2f_sf },
        { "atanf", (uintptr_t)&atanf_sf },
        { "ceil", (uintptr_t)&ceil_sf },
        { "ceilf", (uintptr_t)&ceilf_sf },
        { "cos", (uintptr_t)&cos_sf },
        { "cosf", (uintptr_t)&cosf_sf },
        { "coshf", (uintptr_t)&coshf_sf },
        { "exp", (uintptr_t)&exp_sf },
        { "exp2", (uintptr_t)&exp2_sf },
        { "exp2f", (uintptr_t)&exp2f_sf },
        { "expf", (uintptr_t)&expf_sf },
        { "floor", (uintptr_t)&floor_sf },
        { "floorf", (uintptr_t)&floorf_sf },
        { "fmax", (uintptr_t)&fmax_sf },
        { "fmaxf", (uintptr_t)&fmaxf_sf },
        { "fmin", (uintptr_t)&fmin_sf },
        { "fminf", (uintptr_t)&fminf_sf },
        { "frexpf", (uintptr_t)&frexpf_sf },
        { "fmod", (uintptr_t)&fmod_sf },
        { "fmodf", (uintptr_t)&fmodf_sf },
        { "frexp", (uintptr_t)&frexp_sf },
        { "ldexp", (uintptr_t)&ldexp_sf },
        { "ldexpf", (uintptr_t)&ldexpf_sf },
        { "log", (uintptr_t)&log_sf },
        { "log10", (uintptr_t)&log10_sf },
        { "log10f", (uintptr_t)&log10f_sf },
        { "logf", (uintptr_t)&logf_sf },
        { "lrint", (uintptr_t)&lrint_sf },
        { "lrintf", (uintptr_t)&lrintf_sf },
        { "lround", (uintptr_t)&lround_sf },
        { "lroundf", (uintptr_t)&lroundf_sf },
        { "modf", (uintptr_t)&modf_sf },
        { "modff", (uintptr_t)&modff_sf },
        { "nearbyintf", (uintptr_t)&nearbyintf_sf },
        { "pow", (uintptr_t)&pow_sf },
        { "powf", (uintptr_t)&powf_sf },
        { "rint", (uintptr_t)&rint_sf },
        { "rintf", (uintptr_t)&rintf_sf },
        { "round", (uintptr_t)&round_sf },
        { "roundf", (uintptr_t)&roundf_sf },
        { "scalbn", (uintptr_t)&scalbn_sf },
        { "scalbnf", (uintptr_t)&scalbnf_sf },
        { "sin", (uintptr_t)&sin_sf },
        { "sincos", (uintptr_t)&sincos_sf },
        { "sincosf", (uintptr_t)&sincosf_sf },
        { "sinf", (uintptr_t)&sinf_sf },
        { "sinh", (uintptr_t)&sinh_sf },
        { "sinhf", (uintptr_t)&sinhf_sf },
        { "sqrt", (uintptr_t)&sqrt_sf },
        { "sqrtf", (uintptr_t)&sqrtf_sf },
        { "tan", (uintptr_t)&tan_sf },
        { "tanf", (uintptr_t)&tanf_sf },
        { "tanh", (uintptr_t)&tanh_sf },
        { "tanhf", (uintptr_t)&tanhf_sf },
        { "trunc", (uintptr_t)&trunc_sf },
        { "truncf", (uintptr_t)&truncf_sf },


        // Sockets
        { "accept", (uintptr_t)&accept },
        { "bind", (uintptr_t)&bind },
        { "connect", (uintptr_t)&connect_soloader },
        { "freeaddrinfo", (uintptr_t)&freeaddrinfo_soloader },
        { "gai_strerror", (uintptr_t)&gai_strerror_soloader },
        { "getaddrinfo", (uintptr_t)&getaddrinfo_soloader },
        { "gethostbyaddr", (uintptr_t)&gethostbyaddr },
        { "gethostbyname", (uintptr_t)&gethostbyname_soloader },
        { "gethostbyname_r", (uintptr_t)&gethostbyname_r_soloader },
        { "gethostname", (uintptr_t)&gethostname },
        { "getpeername", (uintptr_t)&getpeername },
        { "getservbyname", (uintptr_t)&getservbyname },
        { "getsockname", (uintptr_t)&getsockname },
        { "getsockopt", (uintptr_t)&getsockopt },
        { "inet_aton", (uintptr_t)&inet_aton },
        { "inet_pton", (uintptr_t)&inet_pton },
        { "inet_ntoa", (uintptr_t)&inet_ntoa },
        { "inet_ntop", (uintptr_t)&inet_ntop },
        { "listen", (uintptr_t)&listen },
        { "poll", (uintptr_t)&poll_soloader },
        { "recv", (uintptr_t)&recv },
        { "recvfrom", (uintptr_t)&recvfrom },
        { "recvmsg", (uintptr_t)&recvmsg },
        { "select", (uintptr_t)&select_soloader },
        { "send", (uintptr_t)&send },
        { "sendmsg", (uintptr_t)&sendmsg },
        { "sendto", (uintptr_t)&sendto },
        { "setsockopt", (uintptr_t)&setsockopt },
        { "shutdown", (uintptr_t)&shutdown },
        { "socket", (uintptr_t)&socket_soloader },


        // Memory
        { "calloc", (uintptr_t)&calloc_soloader },
        { "free", (uintptr_t)&free_soloader },
        { "malloc", (uintptr_t)&malloc_soloader },
        { "memalign", (uintptr_t)&memalign_soloader },
        { "memcmp", (uintptr_t)&sceClibMemcmp },
        { "memcpy", (uintptr_t)&memcpy_guarded },   /* [PvZ2 MEMGUARD] log+skip garbage calls */
        { "memmem", (uintptr_t)&memmem },
        { "memmove", (uintptr_t)&memmove_guarded },
        { "memset", (uintptr_t)&memset_guarded },
        { "mmap", (uintptr_t)&mmap_bytes_soloader },
        { "__mmap2", (uintptr_t)&mmap2_pages_soloader },
        { "munmap", (uintptr_t)&munmap },
        { "realloc", (uintptr_t)&realloc_soloader },
        { "valloc", (uintptr_t)&valloc_soloader },


        // IO
        { "close", (uintptr_t)&close_soloader },
        { "closedir", (uintptr_t)&closedir_soloader },
        { "execv", (uintptr_t)&ret0 },
        { "fclose", (uintptr_t)&fclose_soloader },
        { "fcntl", (uintptr_t)&fcntl_soloader },
        { "fopen", (uintptr_t)&fopen_soloader },
        { "fstat", (uintptr_t)&fstat_soloader },
        { "fsync", (uintptr_t)&fsync_soloader },
        { "ioctl", (uintptr_t)&ioctl_soloader },
        { "__open_2", (uintptr_t)&open_soloader },
        { "open", (uintptr_t)&open_soloader },
        { "opendir", (uintptr_t)&opendir_soloader },
        { "readdir", (uintptr_t)&readdir_soloader },
        { "readdir_r", (uintptr_t)&readdir_r_soloader },
        { "stat", (uintptr_t)&stat_soloader },
        { "statfs", (uintptr_t)&statfs_soloader },
        { "utime", (uintptr_t)&utime_soloader },

        #ifdef USE_SCELIBC_IO
            { "fdopen", (uintptr_t)&sceLibcBridge_fdopen },
            { "feof", (uintptr_t)&sceLibcBridge_feof },
            { "ferror", (uintptr_t)&sceLibcBridge_ferror },
            { "fflush", (uintptr_t)&sceLibcBridge_fflush },
            { "fgetc", (uintptr_t)&sceLibcBridge_fgetc },
            { "fgetpos", (uintptr_t)&sceLibcBridge_fgetpos },
            { "fgets", (uintptr_t)&sceLibcBridge_fgets },
            { "fileno", (uintptr_t)&sceLibcBridge_fileno },
            { "fputc", (uintptr_t)&sceLibcBridge_fputc },
            { "fputs", (uintptr_t)&sceLibcBridge_fputs },
            { "fread", (uintptr_t)&sceLibcBridge_fread },
            { "freopen", (uintptr_t)&sceLibcBridge_freopen },
            { "fseek", (uintptr_t)&sceLibcBridge_fseek },
            { "fsetpos", (uintptr_t)&sceLibcBridge_fsetpos },
            { "ftell", (uintptr_t)&sceLibcBridge_ftell },
            { "fwide", (uintptr_t)&sceLibcBridge_fwide },
            { "fwrite", (uintptr_t)&sceLibcBridge_fwrite },
            { "getc", (uintptr_t)&sceLibcBridge_getc },
            { "getwc", (uintptr_t)&sceLibcBridge_getwc },
            { "putc", (uintptr_t)&sceLibcBridge_putc },
            { "putchar", (uintptr_t)&sceLibcBridge_putchar },
            { "puts", (uintptr_t)&sceLibcBridge_puts },
            { "putwc", (uintptr_t)&sceLibcBridge_putwc },
            { "setvbuf", (uintptr_t)&sceLibcBridge_setvbuf },
            { "ungetc", (uintptr_t)&sceLibcBridge_ungetc },
            { "ungetwc", (uintptr_t)&sceLibcBridge_ungetwc },
        #else
            { "fdopen", (uintptr_t)&fdopen },
            { "feof", (uintptr_t)&feof },
            { "ferror", (uintptr_t)&ferror },
            { "fflush", (uintptr_t)&fflush },
            { "fgetc", (uintptr_t)&fgetc },
            { "fgetpos", (uintptr_t)&fgetpos },
            { "fgets", (uintptr_t)&fgets },
            { "fileno", (uintptr_t)&fileno },
            { "fputc", (uintptr_t)&fputc },
            { "fputs", (uintptr_t)&fputs },
            { "fread", (uintptr_t)&fread },
            { "freopen", (uintptr_t)&freopen },
            { "fseek", (uintptr_t)&fseek },
            { "fsetpos", (uintptr_t)&fsetpos },
            { "ftell", (uintptr_t)&ftell },
            { "fwide", (uintptr_t)&fwide },
            { "fwrite", (uintptr_t)&fwrite },
            { "getc", (uintptr_t)&getc },
            { "getwc", (uintptr_t)&getwc },
            { "putc", (uintptr_t)&putc },
            { "putchar", (uintptr_t)&putchar },
            { "puts", (uintptr_t)&puts },
            { "putwc", (uintptr_t)&putwc },
            { "setvbuf", (uintptr_t)&setvbuf },
            { "ungetc", (uintptr_t)&ungetc },
            { "ungetwc", (uintptr_t)&ungetwc },
        #endif

        { "access", (uintptr_t)&access_soloader },
        { "basename", (uintptr_t)&basename },
        { "chdir", (uintptr_t)&chdir_soloader },
        { "chmod", (uintptr_t)&chmod_soloader },
        { "dup", (uintptr_t)&dup },
        { "fseeko", (uintptr_t)&fseeko32_soloader }, // 32-bit off_t ABI fix (game is 32-bit)
        { "ftello", (uintptr_t)&ftello32_soloader },
        { "ftruncate", (uintptr_t)&ftruncate_soloader },
        { "getcwd", (uintptr_t)&getcwd },
        { "lseek", (uintptr_t)&lseek32_soloader },
        { "lseek64", (uintptr_t)&lseek64_soloader },
        { "lstat", (uintptr_t)&lstat_soloader },
        { "mkdir", (uintptr_t)&mkdir_soloader },
        { "pread", (uintptr_t)&pread_soloader },
        { "pipe", (uintptr_t)&pipe },
        { "pwrite", (uintptr_t)&pwrite_soloader },
        { "read", (uintptr_t)&read_soloader },
        { "realpath", (uintptr_t)&realpath_soloader },
        { "remove", (uintptr_t)&remove_soloader },
        { "rename", (uintptr_t)&rename_soloader },
        { "rewind", (uintptr_t)&rewind },
        { "rmdir", (uintptr_t)&rmdir_soloader },
        { "truncate", (uintptr_t)&truncate_soloader },
        { "unlink", (uintptr_t)&unlink_soloader },
        { "write", (uintptr_t)&write_soloader },
        { "writev", (uintptr_t)&writev_soloader },


        // *printf, *scanf
        { "snprintf", (uintptr_t)&snprintf },
        { "sprintf", (uintptr_t)&sprintf },
        { "vasprintf", (uintptr_t)&vasprintf },
        { "vprintf", (uintptr_t)&vprintf },
        { "vsnprintf", (uintptr_t)&vsnprintf },
        { "vsprintf", (uintptr_t)&vsprintf },
        { "vsscanf", (uintptr_t)&vsscanf },
        { "vswprintf", (uintptr_t)&vswprintf },
        { "printf", (uintptr_t)&sceClibPrintf },
        { "swprintf", (uintptr_t)&swprintf },

        #ifdef USE_SCELIBC_IO
            { "fprintf", (uintptr_t)&sceLibcBridge_fprintf },
            { "fscanf", (uintptr_t)&sceLibcBridge_fscanf },
            { "sscanf", (uintptr_t)&sceLibcBridge_sscanf },
            { "vfprintf", (uintptr_t)&sceLibcBridge_vfprintf },
        #else
            { "fprintf", (uintptr_t)&fprintf },
            { "fscanf", (uintptr_t)&fscanf },
            { "sscanf", (uintptr_t)&sscanf },
            { "vfprintf", (uintptr_t)&vfprintf },
        #endif



        // EGL
        /* In PVR mode, route the game's + SDL2's egl* imports through the
         * PVR-backed shim (egl_pvr_shim.c) so they share the single PVR
         * context pvr_init_gl() created, instead of SDL doing its own
         * (failing) Android-style EGL bring-up. */
#ifdef USE_PVR_PSP2
#define EGLFN(n) (uintptr_t)&pvrshim_##n
#else
#define EGLFN(n) (uintptr_t)&n
#endif
        { "eglBindAPI", EGLFN(eglBindAPI) },
        { "eglChooseConfig", EGLFN(eglChooseConfig) },
        { "eglCreateContext", EGLFN(eglCreateContext) },
        { "eglCreateWindowSurface", EGLFN(eglCreateWindowSurface) },
        { "eglDestroyContext", EGLFN(eglDestroyContext) },
        { "eglDestroySurface", EGLFN(eglDestroySurface) },
        { "eglGetConfigAttrib", EGLFN(eglGetConfigAttrib) },
        { "eglGetConfigs", EGLFN(eglGetConfigs) },
        { "eglGetCurrentContext", EGLFN(eglGetCurrentContext) },
        { "eglGetCurrentDisplay", EGLFN(eglGetCurrentDisplay) },
        { "eglGetDisplay", EGLFN(eglGetDisplay) },
        { "eglGetError", EGLFN(eglGetError) },
        { "eglGetProcAddress", EGLFN(eglGetProcAddress) },
        { "eglInitialize", EGLFN(eglInitialize) },
        { "eglMakeCurrent", EGLFN(eglMakeCurrent) },
        { "eglSwapInterval", EGLFN(eglSwapInterval) },
        { "_eglSwapInterval", EGLFN(eglSwapInterval) },
        { "eglWaitNative", EGLFN(eglWaitNative) },
        { "_eglWaitNative", EGLFN(eglWaitNative) },
        { "eglWaitGL", EGLFN(eglWaitGL) },
        { "_eglWaitGL", EGLFN(eglWaitGL) },
        { "eglQueryContext", EGLFN(eglQueryContext) },
        { "eglQueryString", EGLFN(eglQueryString) },
        { "eglQuerySurface", EGLFN(eglQuerySurface) },
        { "eglSwapBuffers", EGLFN(eglSwapBuffers) },
        { "eglTerminate", EGLFN(eglTerminate) },
#undef EGLFN


        // OpenGL
        { "glActiveTexture", (uintptr_t)&glActiveTexture_soloader },
        { "glAlphaFunc", (uintptr_t)&glAlphaFunc },
        { "glAlphaFuncx", (uintptr_t)&glAlphaFuncx },
        { "glAttachShader", (uintptr_t)&glAttachShader },
        { "glBindAttribLocation", (uintptr_t)&glBindAttribLocation },
        { "glBindBuffer", (uintptr_t)&glBindBuffer },
        { "glBindFramebuffer", (uintptr_t)&glBindFramebuffer_soloader },
        { "glBindFramebufferOES", (uintptr_t)&glBindFramebuffer_soloader },
        { "glBindRenderbuffer", (uintptr_t)&glBindRenderbuffer },
        { "glBindRenderbufferOES", (uintptr_t)&glBindRenderbuffer },
        { "glBindTexture", (uintptr_t)&glBindTexture_soloader },
        { "glBlendColor", (uintptr_t)&glBlendColor },
        { "glBlendEquation", (uintptr_t)&glBlendEquation },
        { "glBlendEquationOES", (uintptr_t)&glBlendEquation },
        { "glBlendEquationSeparate", (uintptr_t)&glBlendEquationSeparate },
        { "glBlendEquationSeparateOES", (uintptr_t)&glBlendEquationSeparate },
        { "glBlendFunc", (uintptr_t)&glBlendFunc },
        { "glBlendFuncSeparate", (uintptr_t)&glBlendFuncSeparate },
        { "glBlendFuncSeparateOES", (uintptr_t)&glBlendFuncSeparate },
        { "glBufferData", (uintptr_t)&glBufferData_soloader },
        { "glBufferSubData", (uintptr_t)&glBufferSubData },
        { "glCheckFramebufferStatus", (uintptr_t)&glCheckFramebufferStatus },
        { "glCheckFramebufferStatusOES", (uintptr_t)&glCheckFramebufferStatus },
        { "glClear", (uintptr_t)&glClear_soloader },
        { "glClearColor", (uintptr_t)&glClearColor_soloader },
        { "glClearColorx", (uintptr_t)&glClearColorx },
        { "glClearDepthf", (uintptr_t)&glClearDepthf },
        { "glClearDepthx", (uintptr_t)&glClearDepthx },
        { "glClearStencil", (uintptr_t)&glClearStencil },
        { "glClientActiveTexture", (uintptr_t)&glClientActiveTexture },
        { "glClipPlanef", (uintptr_t)&glClipPlanef },
        { "glClipPlanex", (uintptr_t)&glClipPlanex },
        { "glColor4f", (uintptr_t)&glColor4f },
        { "glColor4ub", (uintptr_t)&glColor4ub },
        { "glColor4x", (uintptr_t)&glColor4x },
        { "glColorMask", (uintptr_t)&glColorMask },
        { "glColorPointer", (uintptr_t)&glColorPointer },
        { "glCompileShader", (uintptr_t)&glCompileShader_soloader },
        { "glCompressedTexImage2D", (uintptr_t)&glCompressedTexImage2D_soloader },
        { "glCompressedTexSubImage2D", (uintptr_t)&glCompressedTexSubImage2D_soloader },
        { "glCopyTexImage2D", (uintptr_t)&glCopyTexImage2D },
        { "glCopyTexSubImage2D", (uintptr_t)&glCopyTexSubImage2D },
        { "glCreateProgram", (uintptr_t)&glCreateProgram },
        { "glCreateShader", (uintptr_t)&glCreateShader },
        { "glCullFace", (uintptr_t)&glCullFace },
        { "glCurrentPaletteMatrixOES", (uintptr_t)&ret0 },
        { "glDeleteBuffers", (uintptr_t)&glDeleteBuffers },
        { "glDeleteFramebuffers", (uintptr_t)&glDeleteFramebuffers },
        { "glDeleteFramebuffersOES", (uintptr_t)&glDeleteFramebuffers },
        { "glDeleteProgram", (uintptr_t)&glDeleteProgram_soloader },
        { "glIsProgram", (uintptr_t)&glIsProgram_soloader },
        { "glDeleteRenderbuffers", (uintptr_t)&glDeleteRenderbuffers },
        { "glDeleteRenderbuffersOES", (uintptr_t)&glDeleteRenderbuffers },
        { "glDeleteShader", (uintptr_t)&glDeleteShader },
        { "glDeleteSync", (uintptr_t)&glDeleteSync_soloader },
        { "glDeleteTextures", (uintptr_t)&glDeleteTextures_soloader },
        { "glDeleteVertexArrays", (uintptr_t)&glDeleteVertexArrays },
        { "glDeleteVertexArraysOES", (uintptr_t)&glDeleteVertexArraysOES_soloader },
        { "glDepthFunc", (uintptr_t)&glDepthFunc },
        { "glDepthMask", (uintptr_t)&glDepthMask },
        { "glDepthRangef", (uintptr_t)&glDepthRangef },
        { "glDepthRangex", (uintptr_t)&glDepthRangex },
        { "glDetachShader", (uintptr_t)&glDetachShader },
        { "glDisable", (uintptr_t)&glDisable },
        { "glDisableClientState", (uintptr_t)&glDisableClientState },
        { "glDisableVertexAttribArray", (uintptr_t)&glDisableVertexAttribArray },
        { "glDrawArrays", (uintptr_t)&glDrawArrays_soloader },
        { "glDrawElements", (uintptr_t)&glDrawElements_soloader },
        { "glDrawElementsInstanced", (uintptr_t)&glDrawElementsInstanced },
        { "glDrawElementsInstancedEXT", (uintptr_t)&glDrawElementsInstancedEXT_soloader },
        { "glDrawTexfOES", (uintptr_t)&ret0 },
        { "glDrawTexfvOES", (uintptr_t)&ret0 },
        { "glDrawTexiOES", (uintptr_t)&ret0 },
        { "glDrawTexivOES", (uintptr_t)&ret0 },
        { "glDrawTexsOES", (uintptr_t)&ret0 },
        { "glDrawTexsvOES", (uintptr_t)&ret0 },
        { "glDrawTexxOES", (uintptr_t)&ret0 },
        { "glDrawTexxvOES", (uintptr_t)&ret0 },
        { "glEGLImageTargetRenderbufferStorageOES", (uintptr_t)&ret0 },
        { "glEGLImageTargetTexture2DOES", (uintptr_t)&ret0 },
        { "glEnable", (uintptr_t)&glEnable },
        { "glEnableClientState", (uintptr_t)&glEnableClientState },
        { "glEnableVertexAttribArray", (uintptr_t)&glEnableVertexAttribArray },
        { "glFenceSync", (uintptr_t)&glFenceSync_soloader },
        { "glFinish", (uintptr_t)&glFinish_soloader },
        { "glFlush", (uintptr_t)&glFlush_soloader },
        { "glFogf", (uintptr_t)&glFogf },
        { "glFogfv", (uintptr_t)&glFogfv },
        { "glFogx", (uintptr_t)&glFogx },
        { "glFogxv", (uintptr_t)&glFogxv },
        { "glFramebufferRenderbuffer", (uintptr_t)&glFramebufferRenderbuffer },
        { "glFramebufferRenderbufferOES", (uintptr_t)&glFramebufferRenderbuffer },
        { "glFramebufferTexture2D", (uintptr_t)&glFramebufferTexture2D_soloader },
        { "glFramebufferTexture2DOES", (uintptr_t)&glFramebufferTexture2D_soloader },
        { "glFrontFace", (uintptr_t)&glFrontFace },
        { "glFrustumf", (uintptr_t)&glFrustumf },
        { "glFrustumx", (uintptr_t)&glFrustumx },
        { "glGenBuffers", (uintptr_t)&glGenBuffers },
        { "glGenerateMipmap", (uintptr_t)&glGenerateMipmap },
        { "glGenerateMipmapOES", (uintptr_t)&glGenerateMipmap },
        { "glGenFramebuffers", (uintptr_t)&glGenFramebuffers },
        { "glGenFramebuffersOES", (uintptr_t)&glGenFramebuffers },
        { "glGenRenderbuffers", (uintptr_t)&glGenRenderbuffers },
        { "glGenRenderbuffersOES", (uintptr_t)&glGenRenderbuffers },
        { "glGenTextures", (uintptr_t)&glGenTextures },
        { "glGenVertexArrays", (uintptr_t)&glGenVertexArrays },
        { "glGenVertexArraysOES", (uintptr_t)&glGenVertexArraysOES_soloader },
        { "glGetActiveAttrib", (uintptr_t)&glGetActiveAttrib },
        { "glGetActiveUniform", (uintptr_t)&glGetActiveUniform },
        { "glGetAttribLocation", (uintptr_t)&glGetAttribLocation },
        { "glGetBooleanv", (uintptr_t)&glGetBooleanv },
        { "glGetBufferParameteriv", (uintptr_t)&glGetBufferParameteriv },
        { "glGetBufferPointervOES", (uintptr_t)&ret0 },
        { "glGetClipPlanef", (uintptr_t)&ret0 },
        { "glGetClipPlanex", (uintptr_t)&ret0 },
        { "glGetError", (uintptr_t)&glGetError },
        { "glGetFixedv", (uintptr_t)&ret0 },
        { "glGetFloatv", (uintptr_t)&glGetFloatv },
        { "glGetFramebufferAttachmentParameterivOES", (uintptr_t)&glGetFramebufferAttachmentParameteriv },
        { "glGetIntegerv", (uintptr_t)&glGetIntegerv },
        { "glGetLightfv", (uintptr_t)&ret0 },
        { "glGetLightxv", (uintptr_t)&ret0 },
        { "glGetMaterialfv", (uintptr_t)&ret0 },
        { "glGetMaterialxv", (uintptr_t)&ret0 },
        { "glGetPointerv", (uintptr_t)&ret0 },
        { "glGetRenderbufferParameterivOES", (uintptr_t)&glGetRenderbufferParameteriv },
        { "glGetProgramInfoLog", (uintptr_t)&glGetProgramInfoLog },
        { "glGetProgramiv", (uintptr_t)&glGetProgramiv },
        { "glGetShaderInfoLog", (uintptr_t)&glGetShaderInfoLog },
        { "glGetShaderSource", (uintptr_t)&glGetShaderSource },
        { "glGetShaderiv", (uintptr_t)&glGetShaderiv_soloader },
        { "glGetString", (uintptr_t)&glGetString_soloader },
        { "glGetStringi", (uintptr_t)&glGetStringi },
        { "glGetTexEnvfv", (uintptr_t)&ret0 },
        { "glGetTexEnviv", (uintptr_t)&glGetTexEnviv },
        { "glGetTexEnvxv", (uintptr_t)&ret0 },
        { "glGetTexGenfvOES", (uintptr_t)&ret0 },
        { "glGetTexGenivOES", (uintptr_t)&ret0 },
        { "glGetTexGenxvOES", (uintptr_t)&ret0 },
        { "glInvalidateFramebuffer", (uintptr_t)&glInvalidateFramebuffer_soloader },
        { "glDiscardFramebufferEXT", (uintptr_t)&glInvalidateFramebuffer_soloader },
        { "glGetTexParameterfv", (uintptr_t)&glGetTexParameterfv },
        { "glGetTexParameteriv", (uintptr_t)&glGetTexParameteriv },
        { "glGetTexParameterxv", (uintptr_t)&ret0 },
        { "glGetUniformLocation", (uintptr_t)&glGetUniformLocation_soloader },
        { "glHint", (uintptr_t)&glHint },
        { "glIsBuffer", (uintptr_t)&glIsBuffer },
        { "glIsRenderbuffer", (uintptr_t)&glIsRenderbuffer },
        { "glIsEnabled", (uintptr_t)&glIsEnabled },
        { "glIsFramebufferOES", (uintptr_t)&glIsFramebuffer },
        { "glIsRenderbufferOES", (uintptr_t)&glIsRenderbuffer },
        { "glIsTexture", (uintptr_t)&glIsTexture },
        { "glLightf", (uintptr_t)&ret0 },
        { "glLightfv", (uintptr_t)&glLightfv },
        { "glLightModelf", (uintptr_t)&ret0 },
        { "glLightModelfv", (uintptr_t)&glLightModelfv },
        { "glLightModelx", (uintptr_t)&ret0 },
        { "glLightModelxv", (uintptr_t)&glLightModelxv },
        { "glLightx", (uintptr_t)&ret0 },
        { "glLightxv", (uintptr_t)&glLightxv },
        { "glLineWidth", (uintptr_t)&glLineWidth },
        { "glLineWidthx", (uintptr_t)&glLineWidthx },
        { "glLinkProgram", (uintptr_t)&glLinkProgram_soloader },
        { "glLoadIdentity", (uintptr_t)&glLoadIdentity },
        { "glLoadMatrixf", (uintptr_t)&glLoadMatrixf },
        { "glLoadMatrixx", (uintptr_t)&glLoadMatrixx },
        { "glLoadPaletteFromModelViewMatrixOES", (uintptr_t)&ret0 },
        { "glLogicOp", (uintptr_t)&ret0 },
        { "glMapBufferRange", (uintptr_t)&glMapBufferRange },
        { "glMapBuffer", (uintptr_t)&glMapBuffer },
        { "glMapBufferOES", (uintptr_t)&glMapBuffer },
        { "glMaterialf", (uintptr_t)&glMaterialf },
        { "glMaterialfv", (uintptr_t)&glMaterialfv },
        { "glMaterialx", (uintptr_t)&glMaterialx },
        { "glMaterialxv", (uintptr_t)&glMaterialxv },
        { "glMatrixIndexPointerOES", (uintptr_t)&ret0 },
        { "glMatrixMode", (uintptr_t)&glMatrixMode },
        { "glMultiTexCoord4f", (uintptr_t)&ret0 },
        { "glMultiTexCoord4x", (uintptr_t)&ret0},
        { "glMultMatrixf", (uintptr_t)&glMultMatrixf },
        { "glMultMatrixx", (uintptr_t)&glMultMatrixx },
        { "glNormal3f", (uintptr_t)&glNormal3f },
        { "glNormal3x", (uintptr_t)&glNormal3x },
        { "glNormalPointer", (uintptr_t)&glNormalPointer },
        { "glOrthof", (uintptr_t)&glOrthof },
        { "glOrthox", (uintptr_t)&glOrthox },
        { "glPixelStorei", (uintptr_t)&glPixelStorei },
        { "glPointParameterf", (uintptr_t)&ret0 },
        { "glPointParameterfv", (uintptr_t)&ret0 },
        { "glPointParameterx", (uintptr_t)&ret0 },
        { "glPointParameterxv", (uintptr_t)&ret0 },
        { "glPointSize", (uintptr_t)&glPointSize },
        { "glPointSizePointerOES", (uintptr_t)&ret0 },
        { "glPointSizex", (uintptr_t)&glPointSizex },
        { "glPolygonOffset", (uintptr_t)&glPolygonOffset },
        { "glPolygonOffsetx", (uintptr_t)&glPolygonOffsetx },
        { "glPopMatrix", (uintptr_t)&glPopMatrix },
        { "glPushMatrix", (uintptr_t)&glPushMatrix },
        { "glQueryMatrixxOES", (uintptr_t)&ret0 },
        { "glReadPixels", (uintptr_t)&glReadPixels },
        { "glRenderbufferStorage", (uintptr_t)&glRenderbufferStorage },
        { "glRenderbufferStorageOES", (uintptr_t)&glRenderbufferStorage },
        { "glRotatef", (uintptr_t)&glRotatef },
        { "glRotatex", (uintptr_t)&glRotatex },
        { "glSampleCoverage", (uintptr_t)&ret0 },
        { "glSampleCoveragex", (uintptr_t)&ret0 },
        { "glScalef", (uintptr_t)&glScalef },
        { "glScalex", (uintptr_t)&glScalex },
        { "glScissor", (uintptr_t)&glScissor_soloader },
        { "glShadeModel", (uintptr_t)&glShadeModel },
        { "glShaderBinary", (uintptr_t)&ret0 },
        { "glShaderSource", (uintptr_t)&glShaderSource_soloader },
        { "glStencilFunc", (uintptr_t)&glStencilFunc },
        { "glStencilFuncSeparate", (uintptr_t)&glStencilFuncSeparate },
        { "glStencilMask", (uintptr_t)&glStencilMask },
        { "glStencilOp", (uintptr_t)&glStencilOp },
        { "glStencilOpSeparate", (uintptr_t)&glStencilOpSeparate },
        { "glTexCoordPointer", (uintptr_t)&glTexCoordPointer },
        { "glTexEnvf", (uintptr_t)&glTexEnvf },
        { "glTexEnvfv", (uintptr_t)&glTexEnvfv },
        { "glTexEnvi", (uintptr_t)&glTexEnvi },
        { "glTexEnviv", (uintptr_t)&ret0 },
        { "glTexEnvx", (uintptr_t)&glTexEnvx },
        { "glTexEnvxv", (uintptr_t)&glTexEnvxv },
        { "glTexGenfOES", (uintptr_t)&ret0 },
        { "glTexGenfvOES", (uintptr_t)&ret0 },
        { "glTexGeniOES", (uintptr_t)&ret0 },
        { "glTexGenivOES", (uintptr_t)&ret0 },
        { "glTexGenxOES", (uintptr_t)&ret0 },
        { "glTexGenxvOES", (uintptr_t)&ret0 },
        { "glTexStorage2D", (uintptr_t)&glTexStorage2D_soloader },
        { "glTexStorage2DEXT", (uintptr_t)&glTexStorage2D_soloader },
        { "glTexImage2D", (uintptr_t)&glTexImage2D_pvz2 },
        { "glTexParameterf", (uintptr_t)&glTexParameterf_soloader },
        { "glTexParameterfv", (uintptr_t)&glTexParameterfv_soloader },
        { "glTexParameteri", (uintptr_t)&glTexParameteri_soloader },
        { "glTexParameteriv", (uintptr_t)&glTexParameteriv_soloader },
        { "glTexParameterx", (uintptr_t)&glTexParameterx_soloader },
        { "glTexParameterxv", (uintptr_t)&glTexParameterxv_soloader },
        { "glTexSubImage2D", (uintptr_t)&glTexSubImage2D_soloader },
        { "glTranslatef", (uintptr_t)&glTranslatef },
        { "glTranslatex", (uintptr_t)&glTranslatex },
        { "glUniform1f", (uintptr_t)&glUniform1f_soloader },
        { "glUniform1fv", (uintptr_t)&glUniform1fv_soloader },
        { "glUniform1i", (uintptr_t)&glUniform1i_soloader },
        { "glUniform1iv", (uintptr_t)&glUniform1iv_soloader },
        { "glUniform2i", (uintptr_t)&glUniform2i_soloader },
        { "glUniform2f", (uintptr_t)&glUniform2f_soloader },
        { "glUniform2fv", (uintptr_t)&glUniform2fv_soloader },
        { "glUniform2iv", (uintptr_t)&glUniform2iv_soloader },
        { "glUniform3i", (uintptr_t)&glUniform3i_soloader },
        { "glUniform3f", (uintptr_t)&glUniform3f_soloader },
        { "glUniform3fv", (uintptr_t)&glUniform3fv_soloader },
        { "glUniform3iv", (uintptr_t)&glUniform3iv_soloader },
        { "glUniform4i", (uintptr_t)&glUniform4i_soloader },
        { "glUniform4f", (uintptr_t)&glUniform4f_soloader },
        { "glUniform4fv", (uintptr_t)&glUniform4fv_soloader },
        { "glUniform4iv", (uintptr_t)&glUniform4iv_soloader },
        { "glUniformMatrix2fv", (uintptr_t)&glUniformMatrix2fv_soloader },
        { "glUniformMatrix3fv", (uintptr_t)&glUniformMatrix3fv_soloader },
        { "glUniformMatrix4fv", (uintptr_t)&glUniformMatrix4fv_soloader },
        { "glClientWaitSync", (uintptr_t)&glClientWaitSync_soloader },
        { "glUnmapBuffer", (uintptr_t)&glUnmapBuffer },
        { "glUnmapBufferOES", (uintptr_t)&glUnmapBuffer },
        { "glUseProgram", (uintptr_t)&glUseProgram_soloader },
        { "glValidateProgram", (uintptr_t)&glValidateProgram },
        { "glVertexAttribDivisor", (uintptr_t)&glVertexAttribDivisor },
        { "glVertexAttribDivisorEXT", (uintptr_t)&glVertexAttribDivisorEXT_soloader },
        { "glVertexAttrib4f", (uintptr_t)&glVertexAttrib4f },
        { "glVertexAttrib4fv", (uintptr_t)&glVertexAttrib4fv },
        { "glVertexAttribPointer", (uintptr_t)&glVertexAttribPointer_soloader },
        { "glVertexPointer", (uintptr_t)&glVertexPointer },
        { "glViewport", (uintptr_t)&glViewport_soloader },
        { "glWaitSync", (uintptr_t)&glWaitSync_soloader },
        { "glBindVertexArray", (uintptr_t)&glBindVertexArray },
        { "glBindVertexArrayOES", (uintptr_t)&glBindVertexArrayOES_soloader },
        { "glWeightPointerOES", (uintptr_t)&ret0 },


        // OpenSL ES. FMOD can resolve these through direct imports, an
        // explicit libOpenSLES.so handle, or the global dlopen handle.
        { "slCreateEngine", (uintptr_t)&slCreateEngine_soloader },
        { "SL_IID_ENGINE", (uintptr_t)&SL_IID_ENGINE },
        { "SL_IID_OUTPUTMIX", (uintptr_t)&SL_IID_OUTPUTMIX },
        { "SL_IID_PLAY", (uintptr_t)&SL_IID_PLAY },
        { "SL_IID_VOLUME", (uintptr_t)&SL_IID_VOLUME },
        { "SL_IID_BUFFERQUEUE", (uintptr_t)&SL_IID_BUFFERQUEUE },
        { "SL_IID_ANDROIDSIMPLEBUFFERQUEUE", (uintptr_t)&SL_IID_ANDROIDSIMPLEBUFFERQUEUE },
        { "SL_IID_ANDROIDCONFIGURATION", (uintptr_t)&SL_IID_ANDROIDCONFIGURATION },


        // Pthread
        { "pthread_attr_getstack", (uintptr_t)&pthread_attr_getstack_soloader },
        { "pthread_attr_setstack", (uintptr_t)&pthread_attr_setstack_soloader },
        { "pthread_attr_setschedpolicy", (uintptr_t)&pthread_attr_setschedpolicy_soloader },
        { "pthread_attr_getschedparam", (uintptr_t)&pthread_attr_getschedparam_soloader },
        { "pthread_getattr_np", (uintptr_t)&pthread_getattr_np_soloader },
        { "pthread_attr_destroy", (uintptr_t)&pthread_attr_destroy_soloader },
        { "pthread_attr_init", (uintptr_t) &pthread_attr_init_soloader },
        { "pthread_attr_setdetachstate", (uintptr_t) &pthread_attr_setdetachstate_soloader },
        { "pthread_attr_setstacksize", (uintptr_t) &pthread_attr_setstacksize_soloader },
        { "pthread_attr_setschedparam", (uintptr_t)&pthread_attr_setschedparam_soloader },

        { "pthread_condattr_init", (uintptr_t)&pthread_condattr_init_soloader },
        { "pthread_condattr_destroy", (uintptr_t)&pthread_condattr_destroy_soloader },
        { "pthread_cond_broadcast", (uintptr_t) &pthread_cond_broadcast_soloader },
        { "pthread_cond_destroy", (uintptr_t) &pthread_cond_destroy_soloader },
        { "pthread_cond_init", (uintptr_t) &pthread_cond_init_soloader },
        { "pthread_cond_signal", (uintptr_t) &pthread_cond_signal_soloader },
        { "pthread_cond_timedwait", (uintptr_t) &pthread_cond_timedwait_soloader },
        { "pthread_cond_wait", (uintptr_t) &pthread_cond_wait_soloader },

        { "pthread_create", (uintptr_t) &pthread_create_soloader },
        { "pthread_detach", (uintptr_t) &pthread_detach_soloader },
        { "pthread_equal", (uintptr_t) &pthread_equal_soloader },
        { "pthread_exit", (uintptr_t)&pthread_exit },
        { "pthread_getschedparam", (uintptr_t) &pthread_getschedparam_soloader },
        { "pthread_getspecific", (uintptr_t)&pthread_getspecific },
        { "pthread_join", (uintptr_t) &pthread_join_soloader },
        { "pthread_key_create", (uintptr_t)&pthread_key_create },
        { "pthread_key_delete", (uintptr_t)&pthread_key_delete },
        { "pthread_kill", (uintptr_t)&pthread_kill_soloader },

        { "pthread_mutex_destroy", (uintptr_t) &pthread_mutex_destroy_soloader },
        { "pthread_mutex_init", (uintptr_t) &pthread_mutex_init_soloader },
        { "pthread_mutex_lock", (uintptr_t) &pthread_mutex_lock_soloader },
        { "pthread_mutex_trylock", (uintptr_t) &pthread_mutex_trylock_soloader },
        { "pthread_mutex_unlock", (uintptr_t) &pthread_mutex_unlock_soloader },
        { "pthread_mutexattr_destroy", (uintptr_t) &pthread_mutexattr_destroy_soloader },
        { "pthread_mutexattr_init", (uintptr_t) &pthread_mutexattr_init_soloader },
        { "pthread_mutexattr_settype", (uintptr_t) &pthread_mutexattr_settype_soloader },
        { "pthread_mutexattr_setpshared", (uintptr_t) &ret0 },
        { "pthread_once", (uintptr_t)&pthread_once_soloader },

        { "pthread_self", (uintptr_t) &pthread_self_soloader },
        { "pthread_setname_np", (uintptr_t) &pthread_setname_np_soloader },
        { "pthread_setschedparam", (uintptr_t) &pthread_setschedparam_soloader },
        { "pthread_setspecific", (uintptr_t)&pthread_setspecific },
        { "pthread_sigmask", (uintptr_t)&ret0 },

        { "sem_destroy", (uintptr_t) &sem_destroy_soloader },
        { "sem_getvalue", (uintptr_t) &sem_getvalue_soloader },
        { "sem_init", (uintptr_t) &sem_init_soloader },
        { "sem_post", (uintptr_t) &sem_post_soloader },
        { "sem_close", (uintptr_t) &sem_close_soloader },
        { "sem_timedwait", (uintptr_t) &sem_timedwait_soloader },
        { "sem_trywait", (uintptr_t) &sem_trywait_soloader },
        { "sem_wait", (uintptr_t) &sem_wait_soloader },

        { "sched_get_priority_max", (uintptr_t)&sched_get_priority_max_soloader },
        { "sched_get_priority_min", (uintptr_t)&sched_get_priority_min_soloader },
        { "sched_yield", (uintptr_t)&sched_yield },


        // wchar, wctype
        { "btowc", (uintptr_t)&btowc },
        { "iswalpha", (uintptr_t)&iswalpha },
        { "iswcntrl", (uintptr_t)&iswcntrl },
        { "iswctype", (uintptr_t)&iswctype },
        { "iswdigit", (uintptr_t)&iswdigit },
        { "iswdigit", (uintptr_t)&iswdigit },
        { "iswlower", (uintptr_t)&iswlower },
        { "iswprint", (uintptr_t)&iswprint },
        { "iswpunct", (uintptr_t)&iswpunct },
        { "iswspace", (uintptr_t)&iswspace },
        { "iswupper", (uintptr_t)&iswupper },
        { "iswxdigit", (uintptr_t)&iswxdigit },
        { "mbrlen", (uintptr_t)&mbrlen },
        { "mbrtowc", (uintptr_t)&mbrtowc },
        { "mbsnrtowcs", (uintptr_t)&mbsnrtowcs },
        { "mbsrtowcs", (uintptr_t)&mbsrtowcs },
        { "mbstowcs", (uintptr_t)&mbstowcs },
        { "mbtowc", (uintptr_t)&mbtowc },
        { "towlower", (uintptr_t)&towlower },
        { "towupper", (uintptr_t)&towupper },
        { "wcrtomb", (uintptr_t)&wcrtomb },
        { "wcscasecmp", (uintptr_t)&wcscasecmp },
        { "wcscmp", (uintptr_t)&wcscmp },
        { "wcscoll", (uintptr_t)&wcscoll },
        { "wcscpy", (uintptr_t)&wcscpy },
        { "wcsftime", (uintptr_t)&wcsftime },
        { "wcslcat", (uintptr_t)&wcslcat },
        { "wcslcpy", (uintptr_t)&wcslcpy },
        { "wcslen", (uintptr_t)&wcslen },
        { "wcsncasecmp", (uintptr_t)&wcsncasecmp },
        { "wcsncmp", (uintptr_t)&wcsncmp },
        { "wcsncpy", (uintptr_t)&wcsncpy },
        { "wcsnlen", (uintptr_t)&wcsnlen },
        { "wcsnrtombs", (uintptr_t)&wcsnrtombs },
        { "wcsstr", (uintptr_t)&wcsstr },
        { "wcstod", (uintptr_t)&wcstod },
        { "wcstof", (uintptr_t)&wcstof },
        { "wcstol", (uintptr_t)&wcstol },
        { "wcstoll", (uintptr_t)&wcstoll },
        { "wcstombs", (uintptr_t)&wcstombs },
        { "wcstoul", (uintptr_t)&wcstoul },
        { "wcstoull", (uintptr_t)&wcstoull },
        { "wcsxfrm", (uintptr_t)&wcsxfrm },
        { "wctob", (uintptr_t)&wctob },
        { "wctype", (uintptr_t)&wctype },
        { "wmemchr", (uintptr_t)&wmemchr },
        { "wmemcmp", (uintptr_t)&wmemcmp },
        { "wmemcpy", (uintptr_t)&wmemcpy },
        { "wmemmove", (uintptr_t)&wmemmove },
        { "wmemset", (uintptr_t)&wmemset },


        // libdl
        { "dlclose", (uintptr_t)&ret0 },
        { "dlerror", (uintptr_t)&ret0 },
        { "dlopen", (uintptr_t)&dlopen_soloader },
        { "dlsym", (uintptr_t)&dlsym_soloader },


        // Errno
        { "__errno", (uintptr_t)&__errno_soloader },
        { "strerror", (uintptr_t)&strerror_soloader },
        { "strerror_r", (uintptr_t)&strerror_r_soloader },
        { "perror", (uintptr_t)&perror }, // TODO: errno translation


        // Strings — hot engine imports map to SceLibKernel's optimized
        // sceClib* where a drop-in exists (same win as memset/memmove;
        // the engine calls these millions of times during scene decode).
        /* Vita3K does not implement sceClibMemchr. PAM image references use
         * memchr to split "filename|RESOURCE_ID"; returning NULL here makes
         * every PopAnim fail to load, then Play clones a null loading icon.
         * Newlib's implementation also works on physical Vita. */
        { "memchr", (uintptr_t)&memchr },
        { "memrchr", (uintptr_t)&memrchr },
        { "strcasecmp", (uintptr_t)&strcasecmp },
        { "strcat", (uintptr_t)&strcat },
        { "strchr", (uintptr_t)&sceClibStrchr },
        { "strcmp", (uintptr_t)&sceClibStrcmp },
        { "strcoll", (uintptr_t)&strcoll },
        { "strcpy", (uintptr_t)&strcpy },
        { "strcspn", (uintptr_t)&strcspn },
        { "strdup", (uintptr_t)&strdup },
        { "strlcat", (uintptr_t)&strlcat },
        { "strlcpy", (uintptr_t)&strlcpy },
        { "strlen", (uintptr_t)&strlen },
        { "strncasecmp", (uintptr_t)&sceClibStrncasecmp },
        { "strncat", (uintptr_t)&strncat },
        { "strncmp", (uintptr_t)&sceClibStrncmp },
        { "strncpy", (uintptr_t)&sceClibStrncpy },
        { "strnlen", (uintptr_t)&strnlen },
        { "strpbrk", (uintptr_t)&strpbrk },
        { "strrchr", (uintptr_t)&sceClibStrrchr },
        { "strspn", (uintptr_t)&strspn },
        { "strstr", (uintptr_t)&sceClibStrstr },
        { "strtok", (uintptr_t)&strtok },
        { "strtok_r", (uintptr_t)&strtok_r },
        { "strxfrm", (uintptr_t)&strxfrm },


        // Syscalls
        { "fork", (uintptr_t)&fork },
        { "getpagesize", (uintptr_t)&getpagesize },
        { "getauxval", (uintptr_t)&getauxval_soloader },
        { "getegid", (uintptr_t)&getegid_soloader },
        { "geteuid", (uintptr_t)&geteuid_soloader },
        { "getgid", (uintptr_t)&getgid_soloader },
        { "getpid", (uintptr_t)&getpid },
        { "gettid", (uintptr_t)&gettid_soloader },
        { "getuid", (uintptr_t)&getuid_soloader },
        { "getpwuid", (uintptr_t)&getpwuid_soloader },
        { "sbrk", (uintptr_t)&sbrk },
        { "syscall", (uintptr_t)&syscall_soloader },
        { "sysconf", (uintptr_t)&sysconf_soloader },
        { "system", (uintptr_t)&system },
        { "waitpid", (uintptr_t)&ret0 },


        // Time
        { "clock", (uintptr_t)&clock_soloader },
        { "clock_getres", (uintptr_t)&clock_getres_soloader },
        { "clock_gettime", (uintptr_t)&clock_gettime_soloader },
        // Override SDL's high-res timer with the Vita monotonic clock so the game's
        // frame delta-time can never be NaN/frozen (see SDL_GetPerformanceFrequency_soloader).
        { "SDL_GetPerformanceCounter", (uintptr_t)&SDL_GetPerformanceCounter_soloader },
        { "SDL_GetPerformanceFrequency", (uintptr_t)&SDL_GetPerformanceFrequency_soloader },
        { "SDL_GetTicks", (uintptr_t)&SDL_GetTicks_soloader },
        { "difftime", (uintptr_t)&difftime_sf },
        { "asctime", (uintptr_t)&bionic_asctime },
        { "asctime_r", (uintptr_t)&bionic_asctime_r },
        { "ctime_r", (uintptr_t)&bionic_ctime_r },
        { "gettimeofday", (uintptr_t)&gettimeofday },
        { "gmtime", (uintptr_t)&bionic_gmtime },
        { "gmtime64", (uintptr_t)&bionic_gmtime64 },
        { "gmtime_r", (uintptr_t)&bionic_gmtime_r },
        { "localtime", (uintptr_t)&bionic_localtime },
        { "localtime64", (uintptr_t)&bionic_localtime64 },
        { "localtime_r", (uintptr_t)&bionic_localtime_r },
        { "mktime", (uintptr_t)&bionic_mktime },
        { "mktime64", (uintptr_t)&bionic_mktime64 },
        { "nanosleep", (uintptr_t)&nanosleep },
        { "strftime", (uintptr_t)&bionic_strftime },
        { "strptime", (uintptr_t)&bionic_strptime },
        { "time", (uintptr_t)&time },
        { "tzset", (uintptr_t)&tzset },


        // Temp
        { "mkstemp", (uintptr_t)&mkstemp },
        { "mktemp", (uintptr_t)&mktemp_soloader },
        { "tmpfile", (uintptr_t)&tmpfile },
        { "tmpnam", (uintptr_t)&tmpnam },


        // stdlib
        { "abort", (uintptr_t)&abort_soloader },
        { "_exit", (uintptr_t)&exit_soloader },
        { "alarm", (uintptr_t)&ret0 },
        { "atof", (uintptr_t)&atof_sf },
        { "atoi", (uintptr_t)&atoi },
        { "atol", (uintptr_t)&atol },
        { "atoll", (uintptr_t)&atoll },
        { "bsearch", (uintptr_t)&bsearch },
        { "exit", (uintptr_t)&exit_soloader },
        { "lrand48", (uintptr_t)&lrand48 },
        { "prctl", (uintptr_t)&ret0 },
        { "setpriority", (uintptr_t)&setpriority_soloader },
        { "sleep", (uintptr_t)&sleep },
        { "srand48", (uintptr_t)&srand48 },
        { "strtod", (uintptr_t)&strtod_sf },
        { "strtof", (uintptr_t)&strtof_sf },
        { "strtoimax", (uintptr_t)&strtoimax },
        { "strtol", (uintptr_t)&strtol },
        { "strtold", (uintptr_t)&strtold },
        { "strtoll", (uintptr_t)&strtoll },
        { "strtoul", (uintptr_t)&strtoul },
        { "strtoull", (uintptr_t)&strtoull },
        { "strtoumax", (uintptr_t)&strtoumax },
        { "usleep", (uintptr_t)&usleep },

        #ifdef USE_SCELIBC_IO
            { "qsort", (uintptr_t)&sceLibcBridge_qsort },
            { "rand", (uintptr_t)&sceLibcBridge_rand },
            { "srand", (uintptr_t)&sceLibcBridge_srand },
        #else
            { "qsort", (uintptr_t)&qsort },
            { "rand", (uintptr_t)&rand },
            { "srand", (uintptr_t)&srand },
        #endif


        // Env
        { "getenv", (uintptr_t)&getenv_soloader },
        { "setenv", (uintptr_t)&setenv_soloader },


        // Jmp
        { "setjmp", (uintptr_t)&setjmp }, // TODO: May have different struct size?
        { "longjmp", (uintptr_t)&longjmp }, // TODO: May have different struct size?
        { "sigsetjmp", (uintptr_t)&sigsetjmp_soloader },
        { "siglongjmp", (uintptr_t)&siglongjmp_soloader },


        // Signals
        { "bsd_signal", (uintptr_t)&signal },
        { "raise", (uintptr_t)&raise },
        { "sigaction", (uintptr_t)&sigaction },


        // Locale
        { "freelocale", (uintptr_t)&freelocale },
        { "localeconv", (uintptr_t)&localeconv },
        { "newlocale", (uintptr_t)&newlocale },
        { "setlocale", (uintptr_t)&setlocale },
        { "uselocale", (uintptr_t)&uselocale },


        // zlib
        { "adler32", (uintptr_t)&adler32 },
        { "compress", (uintptr_t)&compress },
        { "compressBound", (uintptr_t)&compressBound },
        { "crc32", (uintptr_t)&crc32 },
        { "deflate", (uintptr_t)&deflate },
        { "deflateEnd", (uintptr_t)&deflateEnd },
        { "deflateInit2_", (uintptr_t)&deflateInit2_soloader_ },
        { "deflateInit_", (uintptr_t)&deflateInit_soloader_ },
        { "deflateReset", (uintptr_t)&deflateReset },
        { "gzclose", (uintptr_t)&gzclose_soloader },
        { "gzgets", (uintptr_t)&gzgets_soloader },
        { "gzopen", (uintptr_t)&gzopen_soloader },
        { "inflate", (uintptr_t)&inflate_soloader },
        { "inflateEnd", (uintptr_t)&inflateEnd },
        { "inflateInit2_", (uintptr_t)&inflateInit2_soloader_ },
        { "inflateInit_", (uintptr_t)&inflateInit_soloader_ },
        { "inflateReset", (uintptr_t)&inflateReset },
        { "inflateReset2", (uintptr_t)&inflateReset2_soloader },
        { "uncompress", (uintptr_t)&uncompress },
};

static void *lookup_symbol_soloader_internal(const char *symbol, int log_missing) {
    if (!symbol) {
        return NULL;
    }

#ifdef USE_PVR_PSP2
    /*
     * PVR path: search the real .suprx modules first so their exported
     * EGL/GLES symbols override our static reimplementations.
     *
     * Skip glTexParameterx / glTexParameterxv: the real driver exports
     * these, but we MUST intercept them to downgrade mipmap min-filters
     * (single-level textures with mipmap filters are INCOMPLETE on PVR
     * and sample as flat colours).  Let default_dynlib route them to
     * our glTexParameterx_soloader wrapper instead.
     */
    if (strcmp(symbol, "glTexParameterx") != 0 &&
        strcmp(symbol, "glTexParameterxv") != 0) {
        so_module *pvr_mods[] = {
                &so_mod_pvr_gles2,
                &so_mod_pvr_egl,
                &so_mod_pvr_gpu,
                &so_mod_pvr_gles1,
                &so_mod_pvr_pvr2d,
                &so_mod_pvr_wsegl,
        };
        for (int i = 0; i < (int)(sizeof(pvr_mods) / sizeof(pvr_mods[0])); ++i) {
            if (pvr_mods[i]->text_base) {
                uintptr_t sym = so_symbol(pvr_mods[i], symbol);
                if (sym) {
                    return (void *)sym;
                }
            }
        }
    }
#endif

    for (int i = 0; i < sizeof(default_dynlib) / sizeof(default_dynlib[0]); i++) {
        if (strcmp(symbol, default_dynlib[i].symbol) == 0) {
            return (void *)default_dynlib[i].func;
        }
    }

    // PvZ2 import gap (FORTIFY _chk wrappers + libc/GL stragglers).
    for (int i = 0; i < pvz2_gap_dynlib_count; i++) {
        if (strcmp(symbol, pvz2_gap_dynlib[i].symbol) == 0) {
            return (void *)pvz2_gap_dynlib[i].func;
        }
    }

    // Some titles use dlsym to query symbols from already loaded peer modules.
    so_module *mods[] = {
            &so_mod_pvz2,
            &so_mod_nimble,
            &so_mod_libcpp,
    };

    for (int i = 0; i < (int)(sizeof(mods) / sizeof(mods[0])); ++i) {
        if (mods[i]->text_base) {
            uintptr_t sym = so_symbol(mods[i], symbol);
            if (sym) {
                return (void *)sym;
            }
        }
    }

    if (log_missing) {
        l_error("dlsym: Unknown symbol \"%s\".", symbol);
    }
    return NULL;
}

void *lookup_symbol_soloader_quiet(const char *symbol) {
    return lookup_symbol_soloader_internal(symbol, 0);
}

#define MCSM_DLOPEN_OPENSL_HANDLE ((void *)0x4F534C31u)

static int is_opensl_library_name(const char *filename) {
    if (!filename) {
        return 0;
    }
    const char *base = strrchr(filename, '/');
    base = base ? base + 1 : filename;
    return strcmp(base, "libOpenSLES.so") == 0;
}

void *dlopen_soloader(const char *filename, int flags) {
    (void)flags;
    if (is_opensl_library_name(filename)) {
        static unsigned s_logged = 0;
        if (s_logged++ < 8U) {
            l_info("dlopen: %s -> OpenSL SceAudioOut bridge", filename ? filename : "(null)");
        }
        return MCSM_DLOPEN_OPENSL_HANDLE;
    }

    static unsigned s_logged = 0;
    if (s_logged++ < 16U) {
        l_info("dlopen: %s -> loader global handle", filename ? filename : "(null)");
    }
    return (void *)1;
}

static void *opensl_bridge_dlsym(const char *symbol) {
    if (!symbol) return NULL;
    if (strcmp(symbol, "slCreateEngine") == 0)                   return (void *)&slCreateEngine_soloader_opensl;
    if (strcmp(symbol, "SL_IID_ENGINE") == 0)                    return (void *)&SL_IID_ENGINE;
    if (strcmp(symbol, "SL_IID_OUTPUTMIX") == 0)                 return (void *)&SL_IID_OUTPUTMIX;
    if (strcmp(symbol, "SL_IID_PLAY") == 0)                      return (void *)&SL_IID_PLAY;
    if (strcmp(symbol, "SL_IID_VOLUME") == 0)                    return (void *)&SL_IID_VOLUME;
    if (strcmp(symbol, "SL_IID_BUFFERQUEUE") == 0)               return (void *)&SL_IID_BUFFERQUEUE;
    if (strcmp(symbol, "SL_IID_ANDROIDSIMPLEBUFFERQUEUE") == 0)  return (void *)&SL_IID_ANDROIDSIMPLEBUFFERQUEUE;
    if (strcmp(symbol, "SL_IID_ANDROIDCONFIGURATION") == 0)      return (void *)&SL_IID_ANDROIDCONFIGURATION;
    if (strcmp(symbol, "SL_IID_RECORD") == 0)                    return (void *)&SL_IID_RECORD;
    return NULL;
}

void *dlsym_soloader(void * handle, const char * symbol) {
    if (handle == MCSM_DLOPEN_OPENSL_HANDLE) {
        void *sl = opensl_bridge_dlsym(symbol);
        if (sl) {
            static int s_logged = 0;
            if (s_logged < 16) { l_info("dlsym: OpenSL '%s' -> SceAudioOut bridge", symbol); s_logged++; }
            return sl;
        }
        l_error("dlsym: OpenSL bridge missing symbol \"%s\".", symbol ? symbol : "(null)");
        return NULL;
    }
    {
        void *sl = opensl_bridge_dlsym(symbol);
        if (sl) {
            static int s_logged_global = 0;
            if (s_logged_global < 16) {
                l_info("dlsym: global OpenSL '%s' -> SceAudioOut bridge", symbol);
                s_logged_global++;
            }
            return sl;
        }
    }
    void *r = lookup_symbol_soloader_internal(symbol, 1);
    if (!r) {
        /* Unknown symbol (optional analytics/crash-reporting native, e.g.
         * libcrashlytics external_api_initialize). The engine calls the result
         * without null-checking -> NULL would crash. Hand back a no-op stub so
         * the call is harmless (returns 0). */
        static unsigned s_logged = 0;
        if (s_logged++ < 32U) {
            l_warn("dlsym: '%s' unknown -> ret0 no-op stub", symbol ? symbol : "(null)");
        }
        r = (void *)&ret0;
    }
    return r;
}

int resolve_imports(so_module* mod) {
    __sF_fake[0] = *stdin;
    __sF_fake[1] = *stdout;
    __sF_fake[2] = *stderr;

    /* SINGLE resolve pass over a COMBINED table (shared default_dynlib + the
     * PvZ2 gap), with dependency chaining on (default_dynlib_only=0).
     *
     * Do NOT split this into two so_resolve() calls: so_resolve re-scans every
     * relocation each call, and for any symbol not present in the table passed,
     * it CLOBBERS an already-resolved JUMP_SLOT back to the fatal plt0_stub.
     * A two-pass (default, then gap-only) approach therefore un-resolves every
     * chain-resolved libc++ symbol (operator new, etc.) on the 2nd pass ->
     * "Unknown symbol ???" crash the moment the engine allocates. One combined
     * pass avoids that entirely. */
    static so_default_dynlib *combined = NULL;
    static int combined_n = 0;
    if (!combined) {
        const int nd = (int)(sizeof(default_dynlib) / sizeof(default_dynlib[0]));
        combined_n = nd + pvz2_gap_dynlib_count;
        combined = (so_default_dynlib *)malloc((size_t)combined_n * sizeof(so_default_dynlib));
        if (!combined) {
            telemetry_log("IMPORT_ERROR", "cannot allocate import table");
            return -1;
        }
        memcpy(combined, default_dynlib, (size_t)nd * sizeof(so_default_dynlib));
        memcpy(combined + nd, pvz2_gap_dynlib,
               (size_t)pvz2_gap_dynlib_count * sizeof(so_default_dynlib));
    }
    int result = so_resolve(mod, combined, combined_n * (int)sizeof(so_default_dynlib), 0);
    telemetry_log("IMPORTS", "unresolved=%d (unknown calls never replaced by success stubs)", result);
    return result;
}
