/*
 * dynlib_pvz2_gap.c — PvZ2-specific import gap (vs this tree's dynlib.c).
 *
 * Covers the 72 non-C++ symbols libPVZ2.so imports that dynlib.c's shared
 * table lacks (computed: nm -D libPVZ2.so  MINUS  dynlib.c symbols; see
 * recon/pvz2_GAP.txt). The ~150 std::__ndk1 C++ imports are NOT here — they
 * chain from libc++_shared.so via so_resolve_link (see main.c load order).
 *
 * Philosophy for first boot: map to a real routine ONLY when it's known to
 * exist in the Vita newlib; otherwise stub (ret0) with a TODO. POSIX bits
 * that don't exist on the Vita (symlink/mknod/mprotect/...) are defensive
 * engine calls off the hot path — stubbing gets us booting; correctness is
 * tuned on-device.
 *
 * Wired in dynlib.c resolve_imports() via a second so_resolve pass.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>
#include <math.h>
#include "reimpl/mem.h"   /* memcpy_guarded — [PvZ2 MEMGUARD] */
#include <time.h>
#include "reimpl/bionic_time.h"
#include <signal.h>

#include <so_util/so_util.h>
#include <falso_jni/FalsoJNI.h>

extern int ret0(void);
extern int ret1(void);

/* ---- EA::Nimble bridge, backed by FalsoJNI ----------------------------
 * libPVZ2 imports EA::Nimble::getEnv()/findClass() (from libNimble.so). We do
 * NOT load libNimble.so — its real code data-aborts against our fake JNI — so
 * we provide these two ourselves. getEnv hands back the FalsoJNI JNIEnv*;
 * findClass routes to FalsoJNI's FindClass (returns a fake jclass handle).
 * jni_init() runs before the static ctors that call these, so `jni` is live. */
static void *EA_Nimble_getEnv(void) {
    return &jni;
}
static void *EA_Nimble_findClass(const char *name) {
    if (!name) return NULL;
    return (void *)jni->FindClass(&jni, name);
}

/* ---- stdio data globals -----------------------------------------------
 * Bionic exports stdin/stdout/stderr as FILE* data symbols. Give the engine
 * a pointer cell it can load; a constructor points them at newlib's streams
 * (which are macros, not linkable symbols, so we cannot map them directly). */
static FILE *g_stdin;
static FILE *g_stdout;
static FILE *g_stderr;
__attribute__((constructor)) static void pvz2_init_std_streams(void) {
    g_stdin = stdin; g_stdout = stdout; g_stderr = stderr;
}

/* ---- FORTIFY (_chk) variants: forward to the plain routine ------------- */
static void   *__memcpy_chk_s(void *d, const void *s, size_t n, size_t dn) { (void)dn; return memcpy_guarded(d, s, n); }   /* [PvZ2 MEMGUARD] */
static char   *__strcpy_chk_s(char *d, const char *s, size_t dn) { (void)dn; return strcpy(d, s); }
static char   *__strcat_chk_s(char *d, const char *s, size_t dn) { (void)dn; return strcat(d, s); }
static char   *__strchr_chk_s(const char *s, int c, size_t sl) { (void)sl; return strchr(s, c); }
static size_t  __strlen_chk_s(const char *s, size_t sl) { (void)sl; return strlen(s); }
static size_t  __strlcpy_chk_s(char *d, const char *s, size_t sz, size_t dn) { (void)dn; return strlcpy(d, s, sz); }
static char   *__strncpy_chk2_s(char *d, const char *s, size_t n, size_t dn, size_t sl) { (void)dn; (void)sl; return strncpy(d, s, n); }
static int     __vsnprintf_chk_s(char *d, size_t n, int f, size_t dn, const char *fmt, va_list ap) { (void)f; (void)dn; return vsnprintf(d, n, fmt, ap); }
static int     __vsprintf_chk_s(char *d, int f, size_t dn, const char *fmt, va_list ap) { (void)f; (void)dn; return vsprintf(d, fmt, ap); }
static size_t  __fwrite_chk_s(const void *p, size_t sz, size_t n, FILE *fp, size_t av) { (void)av; return fwrite(p, sz, n, fp); }
static ssize_t __read_chk_s(int fd, void *b, size_t n, size_t dn) { (void)dn; return read(fd, b, n); }
static ssize_t __write_chk_s(int fd, const void *b, size_t n, size_t dn) { (void)dn; return write(fd, b, n); }

/* ---- small real shims -------------------------------------------------- */
static char *dirname_s(char *p) {
    /* minimal: trim to last '/'; returns "." if none. Good enough for the
     * engine's path bookkeeping. */
    if (!p || !*p) return ".";
    char *slash = strrchr(p, '/');
    if (!slash) return ".";
    if (slash == p) { p[1] = '\0'; return p; }
    *slash = '\0';
    return p;
}
static const char *zlibVersion_s(void) { return "1.2.11"; }
static long g_timezone = 0;

so_default_dynlib pvz2_gap_dynlib[] = {
    /* stdio data globals */
    { "stdin",  (uintptr_t)&g_stdin },
    { "stdout", (uintptr_t)&g_stdout },
    { "stderr", (uintptr_t)&g_stderr },

    /* FORTIFY checked variants */
    { "__memcpy_chk",    (uintptr_t)&__memcpy_chk_s },
    { "__strcpy_chk",    (uintptr_t)&__strcpy_chk_s },
    { "__strcat_chk",    (uintptr_t)&__strcat_chk_s },
    { "__strchr_chk",    (uintptr_t)&__strchr_chk_s },
    { "__strlen_chk",    (uintptr_t)&__strlen_chk_s },
    { "__strlcpy_chk",   (uintptr_t)&__strlcpy_chk_s },
    { "__strncpy_chk2",  (uintptr_t)&__strncpy_chk2_s },
    { "__vsnprintf_chk", (uintptr_t)&__vsnprintf_chk_s },
    { "__vsprintf_chk",  (uintptr_t)&__vsprintf_chk_s },
    { "__fwrite_chk",    (uintptr_t)&__fwrite_chk_s },
    { "__read_chk",      (uintptr_t)&__read_chk_s },
    { "__write_chk",     (uintptr_t)&__write_chk_s },
    { "__readlink_chk",  (uintptr_t)&ret0 },   /* readlink unsupported on Vita */
    { "__FD_ISSET_chk",  (uintptr_t)&ret0 },
    { "__FD_SET_chk",    (uintptr_t)&ret0 },

    /* real libc shims present in Vita newlib */
    { "dirname",     (uintptr_t)&dirname_s },
    { "strsep",      (uintptr_t)&strsep },
    { "ctime",       (uintptr_t)&bionic_ctime },
    { "swscanf",     (uintptr_t)&swscanf },
    { "iswalnum",    (uintptr_t)&iswalnum },
    { "nearbyintf",  (uintptr_t)&nearbyintf },
    { "signal",      (uintptr_t)&signal },
    { "zlibVersion", (uintptr_t)&zlibVersion_s },
    { "timezone",    (uintptr_t)&g_timezone },

    /* filesystem/process POSIX not on Vita — defensive calls, safe stubs.
     * TODO: implement any that turn out to matter on-device. */
    { "readlink",     (uintptr_t)&ret0 },
    { "symlink",      (uintptr_t)&ret0 },
    { "link",         (uintptr_t)&ret0 },
    { "mkfifo",       (uintptr_t)&ret0 },
    { "mknod",        (uintptr_t)&ret0 },
    { "fchmod",       (uintptr_t)&ret0 },
    { "fchown",       (uintptr_t)&ret0 },
    { "lchown",       (uintptr_t)&ret0 },
    { "ftruncate64",  (uintptr_t)&ret0 },
    { "utimes",       (uintptr_t)&ret0 },
    { "getppid",      (uintptr_t)&ret1 },   /* nonzero fake ppid */
    { "getpwnam",     (uintptr_t)&ret0 },
    { "getpwuid_r",   (uintptr_t)&ret0 },
    { "getgrgid",     (uintptr_t)&ret0 },
    { "getgrnam",     (uintptr_t)&ret0 },
    { "unsetenv",     (uintptr_t)&ret0 },
    { "timegm",       (uintptr_t)&bionic_timegm },

    /* memory syscalls — no-op advisory / success */
    { "mprotect",  (uintptr_t)&ret0 },
    { "madvise",   (uintptr_t)&ret0 },
    { "mlock",     (uintptr_t)&ret0 },
    { "mremap",    (uintptr_t)&ret0 },

    /* anti-debug / hw / net probes — safe stubs (online is stubbed) */
    { "ptrace",            (uintptr_t)&ret0 },
    { "__register_atfork", (uintptr_t)&ret0 },
    { "__get_h_errno",     (uintptr_t)&ret0 },
    { "__cmsg_nxthdr",     (uintptr_t)&ret0 },
    { "dladdr",            (uintptr_t)&ret0 },
    { "getnameinfo",       (uintptr_t)&ret1 },
    { "if_nametoindex",    (uintptr_t)&ret0 },
    { "recvmmsg",          (uintptr_t)&ret0 },
    { "sendmmsg",          (uintptr_t)&ret0 },

    /* pthread rwlock / condattr — Vita pthread lacks rwlock. Stub to no-op
     * locking for first boot. TODO: back with a real mutex if the engine
     * relies on writer exclusion (data races otherwise). */
    { "pthread_rwlock_init",     (uintptr_t)&ret0 },
    { "pthread_rwlock_destroy",  (uintptr_t)&ret0 },
    { "pthread_rwlock_rdlock",   (uintptr_t)&ret0 },
    { "pthread_rwlock_wrlock",   (uintptr_t)&ret0 },
    { "pthread_rwlock_unlock",   (uintptr_t)&ret0 },
    { "pthread_condattr_init",   (uintptr_t)&ret0 },
    { "pthread_condattr_destroy",(uintptr_t)&ret0 },

    /* EA::Nimble bridge (libNimble.so not loaded — see stubs above) */
    { "_ZN2EA6Nimble6getEnvEv",    (uintptr_t)&EA_Nimble_getEnv },
    { "_ZN2EA6Nimble9findClassEPKc", (uintptr_t)&EA_Nimble_findClass },

    /* C++ ABI extras (libc++ chain covers most; safe fallbacks) */
    { "__cxa_demangle",       (uintptr_t)&ret0 },   /* NULL = "couldn't demangle" */
    { "__cxa_end_cleanup",    (uintptr_t)&ret0 },
    { "__cxa_guard_abort",    (uintptr_t)&ret0 },
    { "__dynamic_cast",       (uintptr_t)&ret0 },   /* TODO: libc++ provides real one */
    { "__emutls_get_address", (uintptr_t)&ret0 },   /* TODO: real emutls if TLS faults */

    /* GLES the engine imports that dynlib.c lacked (vitaGL has them;
     * ret1 = "valid" is a safe placeholder until wired to vitaGL). */
    { "glIsProgram", (uintptr_t)&ret1 },
    { "glIsShader",  (uintptr_t)&ret1 },
    { "gzread",      (uintptr_t)&ret0 },
};

const int pvz2_gap_dynlib_count = (int)(sizeof(pvz2_gap_dynlib) / sizeof(pvz2_gap_dynlib[0]));
