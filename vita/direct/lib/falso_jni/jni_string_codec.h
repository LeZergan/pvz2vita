#ifndef FALSOJNI_STRING_CODEC_H
#define FALSOJNI_STRING_CODEC_H

#include <stdint.h>
#include <stddef.h>
#include <limits.h>

/* JNI encodes UTF-16 code units separately: NUL is C0 80 and a surrogate
 * pair occupies six bytes. This is distinct from the game's UTF-8 UI events. */
static size_t fjni_mutf8_encode(const uint16_t *input, size_t count, char *out) {
    size_t n = 0;
    for (size_t i = 0; i < count; ++i) {
        uint32_t c = input[i];
        size_t bytes = c && c < 0x80 ? 1u : c < 0x800 ? 2u : 3u;
        if (n > SIZE_MAX - bytes) return SIZE_MAX;
        if (out) {
            if (bytes == 1) out[n] = (char)c;
            else if (bytes == 2) {
                out[n] = (char)(0xc0 | (c >> 6));
                out[n + 1] = (char)(0x80 | (c & 0x3f));
            } else {
                out[n] = (char)(0xe0 | (c >> 12));
                out[n + 1] = (char)(0x80 | ((c >> 6) & 0x3f));
                out[n + 2] = (char)(0x80 | (c & 0x3f));
            }
        }
        n += bytes;
    }
    return n;
}

static int fjni_utf8_cont(unsigned char byte) { return (byte & 0xc0) == 0x80; }

/* Accept JNI modified UTF-8, plus the four-byte UTF-8 form used by some native
 * callers. Normalize the latter to a UTF-16 pair and canonical JNI bytes.
 * Malformed sequences are replaced without reading beyond the input. */
static size_t fjni_mutf8_decode(const unsigned char *input, size_t bytes, uint16_t *out) {
    size_t n = 0;
    for (size_t i = 0; i < bytes;) {
        uint32_t c = input[i];
        size_t consumed = 1;
        if (c < 0x80) {
            /* ASCII */
        } else if ((c & 0xe0) == 0xc0 && bytes - i >= 2 && fjni_utf8_cont(input[i + 1])) {
            uint32_t value = ((c & 0x1f) << 6) | (input[i + 1] & 0x3f);
            if (value >= 0x80 || value == 0) { c = value; consumed = 2; }
            else c = 0xfffd;
        } else if ((c & 0xf0) == 0xe0 && bytes - i >= 3 &&
                   fjni_utf8_cont(input[i + 1]) && fjni_utf8_cont(input[i + 2])) {
            uint32_t value = ((c & 0xf) << 12) | ((input[i + 1] & 0x3f) << 6) | (input[i + 2] & 0x3f);
            if (value >= 0x800) { c = value; consumed = 3; }
            else c = 0xfffd;
        } else if ((c & 0xf8) == 0xf0 && bytes - i >= 4 && fjni_utf8_cont(input[i + 1]) &&
                   fjni_utf8_cont(input[i + 2]) && fjni_utf8_cont(input[i + 3])) {
            uint32_t value = ((c & 7) << 18) | ((input[i + 1] & 0x3f) << 12) |
                             ((input[i + 2] & 0x3f) << 6) | (input[i + 3] & 0x3f);
            if (value >= 0x10000 && value <= 0x10ffff) { c = value; consumed = 4; }
            else c = 0xfffd;
        } else c = 0xfffd;
        if (c > 0xffff) {
            c -= 0x10000;
            if (out) { out[n] = (uint16_t)(0xd800 | (c >> 10)); out[n + 1] = (uint16_t)(0xdc00 | (c & 0x3ff)); }
            n += 2;
        } else {
            if (out) out[n] = (uint16_t)c;
            ++n;
        }
        i += consumed;
    }
    return n;
}

#endif
