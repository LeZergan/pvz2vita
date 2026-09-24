#ifndef PVZ2_ETC1_BLOCK_H
#define PVZ2_ETC1_BLOCK_H
#include <stdint.h>
#include <string.h>
static const int k_etc1_mod[8][4] = {
    { 2, 8, -2, -8 }, { 5, 17, -5, -17 }, { 9, 29, -9, -29 }, { 13, 42, -13, -42 },
    { 18, 60, -18, -60 }, { 24, 80, -24, -80 }, { 33, 106, -33, -106 }, { 47, 183, -47, -183 }
};
static inline uint8_t etc1_clamp(int v) { return v < 0 ? 0 : (v > 255 ? 255 : (uint8_t)v); }

static inline void pvz2_etc1_block(const uint8_t *b, uint8_t *out) {
            uint32_t hi = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
            uint32_t pix = ((uint32_t)b[4] << 24) | ((uint32_t)b[5] << 16) | ((uint32_t)b[6] << 8) | b[7];
            int flip = hi & 1, diff = (hi >> 1) & 1;
            int base[2][3];
            if (diff) {
                int r = (hi >> 27) & 0x1f, g = (hi >> 19) & 0x1f, bl = (hi >> 11) & 0x1f;
                int dr = (hi >> 24) & 7, dg = (hi >> 16) & 7, db = (hi >> 8) & 7;
                if (dr > 3) dr -= 8; if (dg > 3) dg -= 8; if (db > 3) db -= 8;
                int r2 = r + dr, g2 = g + dg, b2 = bl + db;
                base[0][0] = (r << 3) | (r >> 2); base[0][1] = (g << 3) | (g >> 2); base[0][2] = (bl << 3) | (bl >> 2);
                base[1][0] = (r2 * 8) | (r2 >> 2); base[1][1] = (g2 * 8) | (g2 >> 2); base[1][2] = (b2 * 8) | (b2 >> 2);
            } else {
                int r1 = (hi >> 28) & 0xf, g1 = (hi >> 20) & 0xf, b1 = (hi >> 12) & 0xf;
                int r2 = (hi >> 24) & 0xf, g2 = (hi >> 16) & 0xf, b2 = (hi >> 8) & 0xf;
                base[0][0] = (r1 << 4) | r1; base[0][1] = (g1 << 4) | g1; base[0][2] = (b1 << 4) | b1;
                base[1][0] = (r2 << 4) | r2; base[1][1] = (g2 << 4) | g2; base[1][2] = (b2 << 4) | b2;
            }
            int cw[2] = { (int)((hi >> 5) & 7), (int)((hi >> 2) & 7) };
            /* Each subblock has only four possible colors. Clamp each once,
             * then copy the selected RGBA value, rather than clamping RGB for
             * every pixel. memcpy keeps unaligned output valid. */
            uint8_t palette[2][4][4];
            for (int sub = 0; sub < 2; ++sub) for (int code = 0; code < 4; ++code) {
                int m = k_etc1_mod[cw[sub]][code];
                palette[sub][code][0] = etc1_clamp(base[sub][0] + m);
                palette[sub][code][1] = etc1_clamp(base[sub][1] + m);
                palette[sub][code][2] = etc1_clamp(base[sub][2] + m);
                palette[sub][code][3] = 255;
            }
            for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
                int i = x * 4 + y;
                int sub = flip ? (y >> 1) : (x >> 1);
                int msb = (pix >> (i + 16)) & 1, lsb = (pix >> i) & 1;
                uint8_t *o = out + (y * 4 + x) * 4u;
                memcpy(o, palette[sub][(msb << 1) | lsb], 4);
            }

}
#endif
