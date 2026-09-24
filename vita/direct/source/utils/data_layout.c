/* Move old generated files once, on the same volume, before game threads or
 * loggers start. Each rename is atomic; interrupted migrations can resume.
 * Conflicting copies are preserved and reported instead of overwriting saves. */
#include "utils/boot_check.h"
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static int keep_at_root(const char *name) {
    return !strcmp(name, ".") || !strcmp(name, "..") || !strcmp(name, "userdata") ||
        !strcmp(name, "logging") || !strcmp(name, "libPVZ2.so") || !strcmp(name, "game.obb") ||
        !strcmp(name, "main.147.com.ea.game.pvz2_row.obb");
}

int pvz2_prepare_userdata(char *error, size_t capacity) {
    const char *marker = DATA_PATH ".layout-v1";
    const char stamp[] = "pvz2-userdata-v1\n";
    char check[sizeof(stamp)] = {0};
    FILE *file = fopen(marker, "rb");
    if (file) {
        size_t n = fread(check, 1, sizeof(stamp) - 1, file);
        fclose(file);
        if (n == sizeof(stamp) - 1 && !memcmp(check, stamp, n)) return 1;
    }
    mkdir("ux0:data", 0777);
    if (mkdir(GAME_DATA_PATH, 0777) != 0 && errno != EEXIST) goto fail;
    if (mkdir(DATA_PATH, 0777) != 0 && errno != EEXIST) goto fail;
    for (;;) {
        DIR *dir = opendir(GAME_DATA_PATH);
        if (!dir) goto fail;
        char name[256] = {0};
        struct dirent *entry;
        errno = 0;
        while ((entry = readdir(dir))) {
            if (!keep_at_root(entry->d_name)) {
                snprintf(name, sizeof(name), "%s", entry->d_name);
                break;
            }
        }
        int read_error = !entry ? errno : 0;
        closedir(dir);
        if (read_error) { errno = read_error; goto fail; }
        if (!*name) break;
        char old_path[512], new_path[512];
        snprintf(old_path, sizeof(old_path), "%s%s", GAME_DATA_PATH, name);
        snprintf(new_path, sizeof(new_path), "%s%s", DATA_PATH, name);
        struct stat st;
        if (stat(new_path, &st) == 0) {
            snprintf(error, capacity, "Two copies of existing data were found:\n%s\nand %s\n\n"
                     "Both copies are safe. Back them up, then keep\nthe wanted copy inside userdata/.\n"
                     "Remove the extra copy from the top folder\nand launch again.", old_path, new_path);
            return 0;
        }
        if (errno != ENOENT || rename(old_path, new_path) != 0) goto fail;
    }
    file = fopen(marker, "wb");
    if (!file) goto fail;
    int ok = fwrite(stamp, 1, sizeof(stamp) - 1, file) == sizeof(stamp) - 1;
    if (fclose(file) != 0) ok = 0;
    if (ok) return 1;
fail:
    snprintf(error, capacity, "Cannot prepare ux0:data/pvz2/userdata/.\n\n"
             "Check free space and your SD2Vita/memory card.\nRestart after fixing storage.\n\n"
             "Existing files are kept; unfinished moves\nwill continue on the next launch.\nError: %d", errno);
    return 0;
}
