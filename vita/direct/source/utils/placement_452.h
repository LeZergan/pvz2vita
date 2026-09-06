#ifndef PVZ2_PLACEMENT_452_H
#define PVZ2_PLACEMENT_452_H

#include <stdint.h>

/* 4.5.2's placement scratch grid is int[9][10]. A relaxed placement uses
 * the minimum footprint, but the native marker subsequently writes the full
 * footprint. Clip that bookkeeping to the grid, retaining placement and the
 * original per-cell max(current, minimum-footprint ? 3 : 1) rule. */
static inline int placement452_mark_cells(int *cells, int x, int y,
                                         int width, int height,
                                         int minimum_width, int minimum_height) {
    int64_t right = (int64_t)x + width, bottom = (int64_t)y + height;
    int clipped = x < 0 || y < 0 || right > 9 || bottom > 10;
    if (width <= 0 || height <= 0) return clipped;
    int first_x = x < 0 ? 0 : x, first_y = y < 0 ? 0 : y;
    int last_x = right > 9 ? 9 : (right < 0 ? 0 : (int)right);
    int last_y = bottom > 10 ? 10 : (bottom < 0 ? 0 : (int)bottom);
    for (int col = first_x; col < last_x; ++col) {
        for (int row = first_y; row < last_y; ++row) {
            int value = (int64_t)col - x < minimum_width &&
                        (int64_t)row - y < minimum_height ? 3 : 1;
            int *cell = cells + col * 10 + row;
            if (*cell < value) *cell = value;
        }
    }
    return clipped;
}

#endif
