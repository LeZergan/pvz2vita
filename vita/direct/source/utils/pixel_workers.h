#ifndef PVZ2_PIXEL_WORKERS_H
#define PVZ2_PIXEL_WORKERS_H
#include <stddef.h>
#include <stdint.h>

enum pvz2_pixel_mode { PVZ2_RGBA_HALF, PVZ2_ALPHA_RGBA, PVZ2_ALPHA_HALF,
                      PVZ2_ETC1_RGBA, PVZ2_ETC1_HALF };
/* Call once on main before game constructors. Failed workers fall back inline. */
void pvz2_pixels_init(unsigned workers);
/* Returns owned RGBA bytes. All readers finish before returning to the uploader. */
uint8_t *pvz2_pixels_convert(const uint8_t *src, int width, int height,
                            enum pvz2_pixel_mode mode);
void pvz2_pixels_format_stats(char *out, size_t size);
int pvz2_pixels_convert_into(const uint8_t *src, int width, int height,
    enum pvz2_pixel_mode mode, uint8_t *dst, size_t capacity);
#endif
