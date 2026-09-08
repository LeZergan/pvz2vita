#ifndef PVZ2_VITA_RSB_INDEX_H
#define PVZ2_VITA_RSB_INDEX_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Exact Resources_GetAssetFileInfo backing index used by PvZ2Native.
 * Returns one when the name exists in the 4.5.2 RSB and fills the absolute
 * OBB byte range.  Texture entries are reported as compressed and must not be
 * exposed as a flat AssetFileDescriptor. */
int vita_rsb_find(const char *guest_path, uint64_t *offset, uint32_t *size,
                  int *compressed);

/* Resolve a regular resource to a readable container. Compressed RSG data is
 * streamed to a local cache before returning its uncompressed byte range. */
const char *vita_rsb_locate(const char *guest_path, uint64_t *offset, uint32_t *size);

const char *vita_rsb_obb_path(void);
/* Read-only index construction overlaps boot on a verified worker core. */
void vita_rsb_start_preload(void);
void vita_rsb_format_stats(char *out, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
