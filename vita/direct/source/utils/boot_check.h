#ifndef PVZ2_BOOT_CHECK_H
#define PVZ2_BOOT_CHECK_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PVZ2_LIBRARY_BYTES 18198492u
#define PVZ2_OBB_BYTES 656855040u
/* Chosen once before workers start. Existing installations keep working. */
const char *pvz2_obb_path(void);
int pvz2_boot_check(char *error, size_t capacity);
int pvz2_prepare_userdata(char *error, size_t capacity);
void pvz2_boot_screen(const char *message) __attribute__((noreturn));
void pvz2_dialog_graphics_ready(void);
#ifdef __cplusplus
}
#endif
#endif
