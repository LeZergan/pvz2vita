#include "reimpl/asset_manager.h"
#include "reimpl/io.h"
#include "utils/logger.h"
#ifdef USE_PVR_PSP2
#include "utils/loading_screen.h"
#endif

#include <pthread.h>
#include <malloc.h>
#include <cstring>
#include <cstdio>
#include <dirent.h>
#include <libc_bridge/libc_bridge.h>
#include <string>
#include <fcntl.h>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <new>

typedef struct assetManager {
    int dummy = 0;
    pthread_mutex_t mLock;
} assetManager;

typedef struct aAsset {
    char * filename;
    FILE* f;
    size_t bytesRead;
    size_t fileSize;
    bool opened = false;
    void *buffer;
    size_t bufferSize;
} aAsset;

typedef struct aAssetDirState {
    char **entries;
    size_t count;
    size_t cursor;
    size_t capacity;
} aAssetDirState;

static assetManager g_asset_manager = {0, PTHREAD_MUTEX_INITIALIZER};

#ifdef USE_SCELIBC_IO
#define asset_seek sceLibcBridge_fseek
#define asset_tell sceLibcBridge_ftell
#define asset_read sceLibcBridge_fread
#define asset_close sceLibcBridge_fclose
#else
#define asset_seek fseek
#define asset_tell ftell
#define asset_read fread
#define asset_close fclose
#endif

static FILE *asset_fopen(const char *path) {
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_fopen(path, "rb");
#else
    return fopen(path, "rb");
#endif
}

static FILE *asset_fopen_with_retry(const char *path) {
    FILE *f = asset_fopen(path);
    if (f) {
        return f;
    }

    int first_errno = errno;
    /* Missing optional assets are normal. Do not evict useful descriptors for
     * ENOENT (or retry permission/media failures) on every fallback lookup. */
    if (first_errno != EMFILE && first_errno != ENFILE) return NULL;
    asset_vfd_trim_cached_fds(8u);
    f = asset_fopen(path);

    static unsigned int s_fail_log = 0;
    unsigned log_index = __atomic_fetch_add(&s_fail_log, 1u, __ATOMIC_RELAXED);
    if (f) {
        if (log_index < 24u) {
            l_info("AAssetManager_open retry succeeded after VFD trim: %s", path);
        }
    } else if (log_index < 24u) {
        l_warn("AAssetManager_open failed: %s errno=%d", path, first_errno);
    }
    return f;
}

static std::string normalize_asset_dir_name(const char *dirName) {
    if (!dirName) return "";
    std::string normalized(dirName);
    while (!normalized.empty() && normalized.front() == '/') normalized.erase(normalized.begin());
    while (!normalized.empty() && normalized.back() == '/') normalized.pop_back();
    return normalized;
}

static int asset_dir_entry_cmp(const void *a, const void *b) {
    const char *lhs = *(const char * const *)a;
    const char *rhs = *(const char * const *)b;
    return strcmp(lhs, rhs);
}

static bool asset_dir_push_entry(aAssetDirState *dir, const char *name) {
    if (dir->count == dir->capacity) {
        if (dir->capacity > SIZE_MAX / (2 * sizeof(char *))) return false;
        size_t capacity = dir->capacity ? dir->capacity * 2 : 16;
        char **new_entries = (char **)realloc(dir->entries, capacity * sizeof(char *));
        if (!new_entries) return false;
        dir->entries = new_entries;
        dir->capacity = capacity;
    }
    dir->entries[dir->count] = strdup(name);
    if (!dir->entries[dir->count]) return false;
    dir->count += 1;
    return true;
}

AAssetManager * AAssetManager_create() {
    return (AAssetManager *)&g_asset_manager;
}

AAssetManager * AAssetManager_fromJava(void *env, void *assetManager) {
    (void)env; (void)assetManager;
    return AAssetManager_create();
}

AAsset* AAssetManager_open(AAssetManager* mgr, const char* filename, int mode) {
#ifdef USE_PVR_PSP2
    if (filename && *filename) { loading_screen_set_asset(filename); loading_screen_tick(); }
#endif
    (void)mgr; (void)mode;
    if (!filename || !*filename) return NULL;
    char path[1024];
    int n = snprintf(path, sizeof(path), "%sassets/%s", DATA_PATH, filename);
    if (n < 0 || (size_t)n >= sizeof(path)) return NULL;
    FILE *f = asset_fopen_with_retry(path);
    if (!f) {
        n = snprintf(path, sizeof(path), "%s%s", DATA_PATH, filename);
        if (n < 0 || (size_t)n >= sizeof(path)) return NULL;
        f = asset_fopen_with_retry(path);
    }
    if (!f) return NULL;
    long length = -1;
    if (asset_seek(f, 0, SEEK_END) == 0) length = asset_tell(f);
    if (length < 0 || asset_seek(f, 0, SEEK_SET) != 0) { asset_close(f); return NULL; }
    auto *a = new (std::nothrow) aAsset{};
    if (!a) { asset_close(f); return NULL; }
    a->filename = strdup(path);
    if (!a->filename) { asset_close(f); delete a; return NULL; }
    a->f = f; a->fileSize = (size_t)length; a->opened = true;
    return (AAsset *)a;
}

/* Descriptor handoff releases the stdio handle to conserve Vita FDs. Reopen
 * lazily at the asset's own position; the exported descriptor is independent. */
static bool asset_ensure_stream(aAsset *a) {
    if (a->opened) return true;
    FILE *f = asset_fopen_with_retry(a->filename);
    if (!f) return false;
    if (asset_seek(f, (long)a->bytesRead, SEEK_SET) != 0) { asset_close(f); return false; }
    a->f = f; a->opened = true;
    return true;
}

void AAsset_close(AAsset* asset) {
    l_debug("AAsset_close<%p>(%p)", __builtin_return_address(0), asset);
    if (asset) {
        aAsset * a = (aAsset *) asset;
        free(a->filename);
        if (a->opened) {
#ifdef USE_SCELIBC_IO
            sceLibcBridge_fclose(a->f);
#else
            fclose(a->f);
#endif
        }
        if (a->buffer) { free(a->buffer); a->buffer = NULL; a->bufferSize = 0; }
        delete a;
    }
}

int AAsset_read(AAsset* asset, void* buf, size_t count) {
    if (!asset) return -1;
    if (!count) return 0;
    if (!buf) return -1;
    aAsset *a = (aAsset *)asset;
    size_t remaining = a->fileSize - a->bytesRead;
    if (count > remaining) count = remaining;
    if (count > INT_MAX) count = INT_MAX;
    if (!count) return 0;
    if (a->buffer) {
        memcpy(buf, (const char *)a->buffer + a->bytesRead, count);
        a->bytesRead += count;
        return (int)count;
    }
    if (!asset_ensure_stream(a)) return -1;
    size_t n = asset_read(buf, 1, count, a->f);
    a->bytesRead += n;
    /* An early EOF is truncated/corrupt asset data, not a valid asset EOF. */
    return n ? (int)n : -1;
}

off_t AAsset_seek(AAsset* asset, off_t offset, int whence) {
    if (!asset) return -1;
    aAsset *a = (aAsset *)asset;
    int64_t base;
    switch (whence) {
        case SEEK_SET: base = 0; break;
        case SEEK_CUR: base = (int64_t)a->bytesRead; break;
        case SEEK_END: base = (int64_t)a->fileSize; break;
        default: return -1;
    }
    /* Check before addition, including OFF_MIN. Android assets reject seeks
     * outside their data, unlike unrestricted stdio files. */
    if (offset < -base || offset > (int64_t)a->fileSize - base) return -1;
    size_t pos = (size_t)(base + offset);
    if (!a->buffer && a->opened && asset_seek(a->f, (long)pos, SEEK_SET) != 0) return -1;
    a->bytesRead = pos;
    return (off_t)pos;
}

off_t AAsset_getRemainingLength(AAsset* asset) {
    if (!asset) return -1;
    aAsset *a = (aAsset *)asset;
    return (off_t)(a->fileSize - a->bytesRead);
}

off_t AAsset_getLength(AAsset* asset) {
    if (!asset) return (off_t)-1;
    aAsset * a = (aAsset *) asset;
    return (off_t)a->fileSize;
}

AAssetDir* AAssetManager_openDir(AAssetManager* mgr, const char* dirName) {
    std::string normalizedDirName = normalize_asset_dir_name(dirName);
    std::string realDirPath = std::string(DATA_PATH) + "assets/";
    std::string fallbackDirPath = std::string(DATA_PATH);
    if (!normalizedDirName.empty()) { realDirPath += normalizedDirName + "/"; fallbackDirPath += normalizedDirName + "/"; }

    auto *dir = new (std::nothrow) aAssetDirState{};
    if (!dir) return NULL;

    DIR *osDir = opendir(realDirPath.c_str());
    bool usingFallbackDir = false;
    if (!osDir) { osDir = opendir(fallbackDirPath.c_str()); usingFallbackDir = osDir != NULL; }
    if (!osDir) {
        l_warn("AAssetManager_openDir<%p>(%p, %s): opendir failed for %s",
               __builtin_return_address(0), mgr, dirName ? dirName : "(null)", realDirPath.c_str());
        return (AAssetDir *)dir;
    }

    struct dirent *entry = NULL;
    while ((entry = readdir(osDir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        std::string assetName;
        if (!normalizedDirName.empty()) assetName = normalizedDirName + "/";
        assetName += entry->d_name;
        if (!asset_dir_push_entry(dir, assetName.c_str())) { l_error("AAssetManager_openDir: failed to append asset entry: %s", assetName.c_str()); closedir(osDir); AAssetDir_close((AAssetDir *)dir); return NULL; }
    }
    closedir(osDir);
    if (dir->count > 1) qsort(dir->entries, dir->count, sizeof(char *), asset_dir_entry_cmp);
    l_debug("AAssetManager_openDir<%p>(%p, %s): %zu entries (%s)",
            __builtin_return_address(0), mgr, dirName ? dirName : "(null)", dir->count, usingFallbackDir ? "fallback" : "assets");
    return (AAssetDir *)dir;
}

const char* AAssetDir_getNextFileName(AAssetDir* assetDir) {
    if (!assetDir) return NULL;
    aAssetDirState *dir = (aAssetDirState *)assetDir;
    if (dir->cursor >= dir->count) return NULL;
    const char *name = dir->entries[dir->cursor];
    dir->cursor += 1;
    return name;
}

void AAssetDir_close(AAssetDir* assetDir) {
    if (assetDir) {
        aAssetDirState *dir = (aAssetDirState *)assetDir;
        for (size_t i = 0; i < dir->count; ++i) free(dir->entries[i]);
        free(dir->entries);
        delete dir;
    }
}

int AAsset_openFileDescriptor(AAsset* asset, off_t* outStart, off_t* outLength) {
    if (!asset) { l_warn("AAsset_openFileDescriptor(%p, %p, %p): asset is null", asset, outStart, outLength); return -1; }
    aAsset * a = (aAsset *) asset;
    if (outStart) *outStart = 0;
    if (outLength) *outLength = a->fileSize;
    if (a->opened) {
#ifdef USE_SCELIBC_IO
        sceLibcBridge_fclose(a->f);
#else
        fclose(a->f);
#endif
        a->opened = false;
        a->f = NULL;
    }
    int ret = asset_vfd_open(a->filename, (off_t)a->fileSize);
    l_debug("AAsset_openFileDescriptor(%p/\"%s\", %p, %p): ret %i", asset, a->filename, outStart, outLength, ret);
    return ret;
}

const void * AAsset_getBuffer(AAsset* asset) {
    if (!asset) return NULL;
    aAsset *a = (aAsset *)asset;
    if (a->buffer) return a->buffer;
    if (!a->fileSize || !asset_ensure_stream(a)) return NULL;
    void *buf = malloc(a->fileSize);
    if (!buf) return NULL;
    if (asset_seek(a->f, 0, SEEK_SET) != 0) { free(buf); return NULL; }
    size_t n = asset_read(buf, 1, a->fileSize, a->f);
    /* Once materialized, reads/seeks use the existing buffer, with no second
     * cache and no stdio/FD cost. Loading a buffer does not consume bytes. */
    if (n == a->fileSize) {
        a->buffer = buf; a->bufferSize = a->fileSize;
        asset_close(a->f); a->f = NULL; a->opened = false;
        return buf;
    }
    free(buf);
    /* A failed load must not change the logical cursor either. A fresh stream
     * will restore it, even when the failing stream cannot seek anymore. */
    asset_close(a->f); a->f = NULL; a->opened = false;
    return NULL;
}
