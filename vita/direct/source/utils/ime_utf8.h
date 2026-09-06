#ifndef PVZ2_IME_UTF8_H
#define PVZ2_IME_UTF8_H
#include <stdint.h>
#include <stddef.h>
/* Never split a UTF-8 sequence; replace malformed UTF-16 with U+FFFD. */
static size_t ime_utf8(const uint16_t *src, size_t units, char *dst, size_t cap) {
    size_t n = 0;
    if (!cap) return 0;
    for (size_t i = 0; i < units && src[i]; ++i) {
        uint32_t c = src[i];
        if (c >= 0xd800 && c <= 0xdbff) {
            if (i + 1 < units && src[i+1] >= 0xdc00 && src[i+1] <= 0xdfff) {
                c = 0x10000 + ((c - 0xd800) << 10) + src[++i] - 0xdc00;
            } else c = 0xfffd;
        } else if (c >= 0xdc00 && c <= 0xdfff) c = 0xfffd;
        unsigned len = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
        if (n + len >= cap) break;
        if (len == 1) dst[n++] = (char)c;
        else {
            dst[n++] = (char)((len == 2 ? 0xc0 : len == 3 ? 0xe0 : 0xf0) | (c >> (6 * (len-1))));
            for (unsigned j = len-1; j; --j) dst[n++] = (char)(0x80 | ((c >> (6*(j-1))) & 63));
        }
    }
    dst[n] = 0;
    return n;
}
#endif
