/* Frozen RC14 serial decoder, used only as a regression oracle. */
static const int reference_mod[8][4] = {
    { 2, 8, -2, -8 }, { 5, 17, -5, -17 }, { 9, 29, -9, -29 }, { 13, 42, -13, -42 },
    { 18, 60, -18, -60 }, { 24, 80, -24, -80 }, { 33, 106, -33, -106 }, { 47, 183, -47, -183 }
};
static inline uint8_t reference_clamp(int v) { return v < 0 ? 0 : (v > 255 ? 255 : (uint8_t)v); }
/* ★ v1321: REUSED heap scratch for the ETC1 decode (allocated once, grown rarely, NEVER
 * freed per-decode) so the big transient RGBA8 buffers (up to 8MB) don't churn/fragment
 * the heap. Fixes the real-Vita frame-7 OOM (heap ~99% used but largest_free only 14KB ->
 * a 1MB tex malloc failed). One stable block instead of 9x malloc/free; caller must NOT free. */
static uint8_t *g_reference_scratch = NULL;
static size_t   g_reference_scratch_sz = 0;
static uint8_t *reference_scratch(size_t need) {
    if (need > g_reference_scratch_sz) {
        uint8_t *nb = (uint8_t *)malloc(need);
        if (!nb) return NULL;                       /* keep the old buffer if a grow fails */
        if (g_reference_scratch) free(g_reference_scratch);
        g_reference_scratch = nb; g_reference_scratch_sz = need;
    }
    return g_reference_scratch;
}
static uint8_t *reference_decode(const uint8_t *src, int w, int h) {
    uint8_t *out = reference_scratch((size_t)w * (size_t)h * 4u);   /* reused, never freed per-call */
    if (!out) return NULL;
    int bw = (w + 3) / 4, bh = (h + 3) / 4;
    for (int by = 0; by < bh; by++) {
        for (int bx = 0; bx < bw; bx++) {
            const uint8_t *b = src + (size_t)((by * bw) + bx) * 8u;
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
                base[1][0] = (r2 << 3) | (r2 >> 2); base[1][1] = (g2 << 3) | (g2 >> 2); base[1][2] = (b2 << 3) | (b2 >> 2);
            } else {
                int r1 = (hi >> 28) & 0xf, g1 = (hi >> 20) & 0xf, b1 = (hi >> 12) & 0xf;
                int r2 = (hi >> 24) & 0xf, g2 = (hi >> 16) & 0xf, b2 = (hi >> 8) & 0xf;
                base[0][0] = (r1 << 4) | r1; base[0][1] = (g1 << 4) | g1; base[0][2] = (b1 << 4) | b1;
                base[1][0] = (r2 << 4) | r2; base[1][1] = (g2 << 4) | g2; base[1][2] = (b2 << 4) | b2;
            }
            int cw[2] = { (int)((hi >> 5) & 7), (int)((hi >> 2) & 7) };
            for (int i = 0; i < 16; i++) {
                int x = i >> 2, y = i & 3;
                int px = bx * 4 + x, py = by * 4 + y;
                if (px >= w || py >= h) continue;
                int sub = flip ? (y >> 1) : (x >> 1);
                int msb = (pix >> (i + 16)) & 1, lsb = (pix >> i) & 1;
                int m = reference_mod[cw[sub]][(msb << 1) | lsb];
                uint8_t *o = out + ((size_t)py * w + px) * 4u;
                o[0] = reference_clamp(base[sub][0] + m);
                o[1] = reference_clamp(base[sub][1] + m);
                o[2] = reference_clamp(base[sub][2] + m);
                o[3] = 255;
            }
        }
    }
    return out;
}
