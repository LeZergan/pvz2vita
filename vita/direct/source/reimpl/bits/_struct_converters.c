/*
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  _struct_converters.c
 * @brief Converters for `dirent` struct, `stat` struct, and `open()` flags
 *        that deal with newlib (Vita) and bionic (Android) incompatibilities.
 *
 * This file has to be `#include`d in `reimpl/io.c` and not compiled on its own.
 */

#define SC_INLINE static inline __attribute__((always_inline))
#if defined(__arm__)
_Static_assert(sizeof(stat64_bionic) == 104, "Android ARMv7 stat size");
_Static_assert(offsetof(stat64_bionic, st_size) == 48, "Android ARMv7 stat size offset");
#endif

#define BIONIC_O_WRONLY                                          01
#define BIONIC_O_RDWR                                            02
#define BIONIC_O_CREAT                                         0100
#define BIONIC_O_EXCL                                          0200
#define BIONIC_O_TRUNC                                        01000
#define BIONIC_O_APPEND                                       02000
#define BIONIC_O_NONBLOCK                                     04000
#define BIONIC_O_DIRECTORY                                 00200000
#define BIONIC__O_TMPFILE                                 020000000
#define BIONIC_O_TMPFILE   (BIONIC__O_TMPFILE | BIONIC_O_DIRECTORY)

/**
 * Convert bionic (Android) `open()` flags to newlib (Vita) flags
 *
 * @param[in] flags open() flags created using musl defines
 *
 * @return open(flags) recreated using newlib defines
 */
SC_INLINE int oflags_bionic_to_newlib(int flags) {
    int out = 0;
    if (flags & BIONIC_O_RDWR)
        out |= O_RDWR;
    else if (flags & BIONIC_O_WRONLY)
        out |= O_WRONLY;
    else
        out |= O_RDONLY;
    if (flags & BIONIC_O_NONBLOCK)
        out |= O_NONBLOCK;
    if (flags & BIONIC_O_APPEND)
        out |= O_APPEND;
    if ((flags & BIONIC_O_CREAT) || (flags & BIONIC_O_TMPFILE))
        out |= O_CREAT;
    if (flags & BIONIC_O_TRUNC)
        out |= O_TRUNC;
    if (flags & BIONIC_O_EXCL)
        out |= O_EXCL;
    return out;
}

/**
 * Convert newlib (Vita) `dirent` struct to bionic (Android) format.
 *
 * @param[in] dirent_newlib Pointer to a newlib-format dirent struct
 *
 * @param[out] ret Caller-owned Android entry. No allocation per file.
 */
SC_INLINE
void dirent_newlib_to_bionic(const struct dirent* dirent_newlib, dirent64_bionic *ret) {
    memset(ret, 0, sizeof(*ret));
    size_t length = strnlen(dirent_newlib->d_name, sizeof(ret->d_name)-1);
    memcpy(ret->d_name, dirent_newlib->d_name, length);
    /* Vita exposes no inode here. Stable nonzero names avoid looking like a
     * deleted directory entry to Android callers that check d_ino. */
    uint64_t inode = UINT64_C(14695981039346656037);
    for (size_t i=0; i<length; ++i) inode=(inode^(unsigned char)ret->d_name[i])*UINT64_C(1099511628211);
    ret->d_ino=inode ? inode : 1;
    ret->d_reclen = (uint16_t)((offsetof(dirent64_bionic,d_name)+length+1+7)&~7u);
    ret->d_type = SCE_S_ISDIR(dirent_newlib->d_stat.st_mode) ? DT_DIR : DT_REG;
}

/**
 * Convert newlib (Vita) `stat` struct to bionic (Android) format.
 * @param[in]  src Pointer to a newlib-format stat struct
 * @param[out] dst Pointer to a bionic-format stat struct
 */
SC_INLINE
void stat_newlib_to_bionic(const struct stat * src, stat64_bionic * dst) {
    memset(dst, 0, sizeof(*dst));
    dst->st_dev = src->st_dev;
    dst->__st_ino = src->st_ino;
    dst->st_ino = src->st_ino;
    dst->st_mode = src->st_mode;
    dst->st_nlink = src->st_nlink;
    dst->st_uid = src->st_uid;
    dst->st_gid = src->st_gid;
    dst->st_rdev = src->st_rdev;
    dst->st_size = src->st_size;
    dst->st_blksize = src->st_blksize;
    dst->st_blocks = src->st_blocks;
    dst->st_atime = (unsigned long)src->st_atime;
    dst->st_atime_nsec = 0;
    dst->st_mtime = (unsigned long)src->st_mtime;
    dst->st_mtime_nsec = 0;
    dst->st_ctime = (unsigned long)src->st_ctime;
    dst->st_ctime_nsec = 0;
}
