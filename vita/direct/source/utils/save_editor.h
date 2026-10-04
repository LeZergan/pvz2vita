#ifndef PVZ2_SAVE_EDITOR_H
#define PVZ2_SAVE_EDITOR_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    char name[96];
    uint32_t coins, gems;
} Pvz2SaveProfile;
/* Startup only, before any game constructors/threads can access the save. */
int pvz2_save_recover(const char *path, char *error, size_t capacity);
int pvz2_save_read(const char *path, Pvz2SaveProfile *profiles, unsigned capacity,
                   char *error, size_t error_capacity);
int pvz2_save_apply(const char *path, unsigned profile, uint32_t coins, uint32_t gems,
                    char *error, size_t capacity);
/* Commit all selected profiles in one backed-up transaction. Unselected
 * profiles (including balances above the editor range) remain untouched. */
int pvz2_save_apply_batch(const char *path, const Pvz2SaveProfile *profiles,
                          unsigned count, uint32_t selected_mask,
                          char *error, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
