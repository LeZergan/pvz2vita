#ifndef PVZ2_TEXT_FIELD_452_H
#define PVZ2_TEXT_FIELD_452_H
#include <stdint.h>
#include <string.h>

/* Exact 4.5.2 libc++ 32-bit wstring layout, verified in b1f7a8/b1eedc.
 * No characters or heap pointers need to be read to select the existing value. */
static inline uint32_t text452_length(const void *string) {
    const unsigned char *s = string;
    uint32_t length;
    if (!(s[0] & 1)) return s[0] >> 1;
    memcpy(&length, s + 4, sizeof(length));
    return length;
}
static inline int text452_select_all(void *widget) {
    unsigned char *w = widget;
    uint32_t length = text452_length(w + 0x88), zero = 0;
    if (length > 65535) return 0;
    memcpy(w + 0xc8, &length, sizeof(length)); /* mCursorPos */
    memcpy(w + 0xcc, &zero, sizeof(zero));    /* mHilitePos */
    return 1;
}
static inline const void *text452_data(const void *string) {
    const unsigned char *s = string;
    uint32_t pointer;
    if (!(s[0] & 1)) return s + 4;
    memcpy(&pointer, s + 8, sizeof(pointer));
    return (const void *)(uintptr_t)pointer;
}
static inline int text452_equal(const void *a, const void *b) {
    uint32_t n = text452_length(a);
    if (n != text452_length(b) || n > 65535) return 0;
    return !n || !memcmp(text452_data(a), text452_data(b), (size_t)n * 4);
}
#endif
