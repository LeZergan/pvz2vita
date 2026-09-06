/* Fast setup checks: small header reads, never a full OBB scan at boot. */
#include "utils/boot_check.h"
#include "utils/utils.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <psp2/io/stat.h>

static const char *obb_path = GAME_DATA_PATH "game.obb";
const char *pvz2_obb_path(void) { return obb_path; }

static int check_file(const char *path, uint32_t expected, int library,
                      char *error, size_t capacity) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        snprintf(error, capacity, "Cannot open %s\n\nCopy the pvz2 folder into ux0:data/.\n"
                 "Keep its files directly inside ux0:data/pvz2/.\n\n"
                 "Required: PvZ2 4.5.2 ROW (version 147).\nRead error: %d", path, errno);
        return 0;
    }
    int ok = fseek(file, 0, SEEK_END) == 0;
    long bytes = ok ? ftell(file) : -1;
    if (bytes != (long)expected) {
        snprintf(error, capacity, "Wrong or incomplete game file:\n%s\n\n"
                 "Expected: %u bytes\nFound: %ld bytes\n\n"
                 "Copy this file again from PvZ2 4.5.2 ROW\n(version 147), then launch again.",
                 path, expected, bytes);
        fclose(file);
        return 0;
    }
    unsigned char header[32];
    rewind(file);
    ok = fread(header, 1, sizeof(header), file) == sizeof(header);
    if (library) {
        const unsigned char elf[] = {0x7f, 'E', 'L', 'F', 1, 1, 1};
        uint64_t init = 0, draw = 0;
        ok = ok && !memcmp(header, elf, sizeof(elf)) && header[18] == 40 && header[19] == 0;
        ok = ok && fseek(file, 0xcc033c, SEEK_SET) == 0 && fread(&init, 8, 1, file) == 1;
        ok = ok && fseek(file, 0xcc7e60, SEEK_SET) == 0 && fread(&draw, 8, 1, file) == 1;
        ok = ok && init == UINT64_C(0xE24DD084E92D4FF0) && draw == UINT64_C(0xE59F1010E59F0010);
    } else {
        ok = ok && !memcmp(header, "1bsr\x04\x00\x00\x00", 8);
    }
    fclose(file);
    if (!ok) snprintf(error, capacity, "Unsupported or damaged game file:\n%s\n\n"
                      "Use the original PvZ2 4.5.2 ROW\n(version 147) game files.\n"
                      "The library must be the ARMv7 version.", path);
    return ok;
}

int pvz2_boot_check(char *error, size_t capacity) {
    if (!error || !capacity) return 0;
    error[0] = 0;
    sceIoMkdir("ux0:data", 0777);
    sceIoMkdir(DATA_PATH, 0777);
    if (!module_loaded("kubridge")) {
        snprintf(error, capacity, "Missing system plugin: kubridge.skprx\n\n"
                 "Install kubridge as a *KERNEL plugin\nin ur0:tai/config.txt.\n"
                 "Reboot your Vita after installing it.\n\n"
                 "Copying the plugin file alone is not enough.");
        return 0;
    }
    if (!file_exists("ur0:data/libshacccg.suprx") &&
        !file_exists("ur0:data/external/libshacccg.suprx")) {
        snprintf(error, capacity, "Missing shader compiler: libshacccg.suprx\n\n"
                 "Run ShaRKBR33D on your Vita to install it.\n\n"
                 "Expected location:\nur0:data/libshacccg.suprx\n"
                 "or ur0:data/external/libshacccg.suprx\n\nThen launch the game again.");
        return 0;
    }
    if (!check_file(GAME_DATA_PATH "libPVZ2.so", PVZ2_LIBRARY_BYTES, 1, error, capacity)) return 0;
    /* Prefer the simple name; never silently ignore a corrupt newer copy. */
    obb_path = file_exists(GAME_DATA_PATH "game.obb") ? GAME_DATA_PATH "game.obb" :
               GAME_DATA_PATH "main.147.com.ea.game.pvz2_row.obb";
    if (!file_exists(obb_path)) obb_path = GAME_DATA_PATH "game.obb";
    if (!check_file(obb_path, PVZ2_OBB_BYTES, 0, error, capacity)) return 0;

    /* No save is touched. Failure is actionable before the game creates one. */
    const char *directories[] = {DATA_PATH, DATA_PATH "No_Backup", DATA_PATH "cache"};
    for (unsigned i = 0; i < sizeof(directories) / sizeof(directories[0]); ++i) {
        sceIoMkdir(directories[i], 0777);
        char path[256];
        snprintf(path, sizeof(path), "%s%s.pvz2-write-check.tmp", directories[i],
                 i == 0 ? "" : "/");
        FILE *file = fopen(path, "wb");
        int saved_errno = errno;
        int ok = 0;
        if (file) {
            ok = fwrite("check", 1, 5, file) == 5;
            if (fclose(file) != 0) ok = 0;
            saved_errno = errno;
            remove(path);
        }
        if (!ok) {
            snprintf(error, capacity, "Cannot write game data or saves:\n%s\n\n"
                     "Check free space on ux0 and reconnect your\nSD2Vita or memory card.\n"
                     "Leave free space for saves and cache files.\n\nWrite error: %d",
                     directories[i], saved_errno);
            return 0;
        }
    }
    return 1;
}
