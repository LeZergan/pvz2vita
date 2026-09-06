#ifndef PVZ2_BOUNDED_LOG_H
#define PVZ2_BOUNDED_LOG_H

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <stddef.h>
#include <stdint.h>

/* Called under the owning logger's mutex. One current file plus one previous
 * file, each at most 2 MiB, even across repeated boots. */
#define PVZ2_LOG_LIMIT (2U * 1024U * 1024U)
static inline void bounded_log_write(SceUID *fd, const char *path,
                                     const void *data, size_t size) {
    if (!data || !size || size > PVZ2_LOG_LIMIT) return;
    if (*fd < 0)
        *fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0644);
    if (*fd < 0) return;
    SceOff position = sceIoLseek(*fd, 0, SCE_SEEK_END);
    if (position < 0) return;
    if ((uint64_t)position > PVZ2_LOG_LIMIT - size) {
        sceIoClose(*fd);
        char previous[256];
        sceClibSnprintf(previous, sizeof(previous), "%s.previous", path);
        sceIoRemove(previous);
        /* Do not preserve an oversized log left by an old build. */
        if ((uint64_t)position <= PVZ2_LOG_LIMIT) sceIoRename(path, previous);
        *fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0644);
        if (*fd < 0) return;
    }
    sceIoWrite(*fd, data, size);
}

#endif
