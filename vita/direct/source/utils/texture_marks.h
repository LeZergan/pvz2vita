#ifndef PVZ2_TEXTURE_MARKS_H
#define PVZ2_TEXTURE_MARKS_H
#include <stdint.h>
#include <stddef.h>

/* Delete from a linear-probed texture-ID set without breaking collision chains.
 * IDs are recycled by GL. A stale failed-upload mark would skip every fill of
 * a completely unrelated texture that later receives the same ID. */
static inline void texture_marks_remove(uint32_t *table, size_t slots, uint32_t id) {
    if (!id) return;
    size_t mask = slots - 1;
    size_t hole = (id * 2654435761u) & mask;
    size_t scanned;
    for (scanned = 0; scanned < slots; ++scanned, hole = (hole + 1) & mask) {
        if (table[hole] == id) break;
        if (!table[hole]) return;
    }
    if (scanned == slots) return;
    size_t next = (hole + 1) & mask;
    for (scanned = 0; scanned < slots - 1 && table[next]; ++scanned, next = (next + 1) & mask) {
        size_t home = (table[next] * 2654435761u) & mask;
        if (((next - home) & mask) >= ((next - hole) & mask)) {
            table[hole] = table[next];
            hole = next;
        }
    }
    table[hole] = 0;
}
#endif
