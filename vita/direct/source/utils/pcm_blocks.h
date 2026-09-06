/* Preserve stream continuity across arbitrary PCM packet boundaries. */
#ifndef PVZ2_PCM_BLOCKS_H
#define PVZ2_PCM_BLOCKS_H
#include <stdint.h>
#include <string.h>
typedef struct {
    int16_t samples[2][4096 * 2];
    unsigned pending, bank;
} PcmBlocks;
typedef int (*PcmBlockOutput)(const int16_t *, void *);
static int pcm_blocks_write(PcmBlocks *p, const int16_t *src, unsigned frames,
                            unsigned channels, unsigned block, int gain,
                            PcmBlockOutput output, void *ctx) {
    if (!src || channels < 1 || channels > 2 || !block || block > 4096) return -1;
    while (frames) {
        unsigned n = block - p->pending;
        if (n > frames) n = frames;
        int16_t *dst = p->samples[p->bank] + p->pending * channels;
        for (unsigned i = 0; i < n * channels; ++i) {
            int value = (int)src[i] * gain / 256;
            dst[i] = value > 32767 ? 32767 : value < -32768 ? -32768 : value;
        }
        p->pending += n;
        src += n * channels;
        frames -= n;
        if (p->pending == block) {
            int rc = output(p->samples[p->bank], ctx);
            p->pending = 0;
            p->bank ^= 1; /* Keep the preceding output intact during the next submit. */
            if (rc < 0) return rc;
        }
    }
    return 0;
}
#endif
