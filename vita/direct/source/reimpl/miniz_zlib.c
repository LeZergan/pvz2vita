#include "reimpl/miniz_zlib.h"

#include "../../third_party/miniz/miniz_tdef.c"
#include "../../third_party/miniz/miniz_tinfl.c"
#include "../../third_party/miniz/miniz.c"

#include <stdint.h>
#include "utils/logger.h"
#include "utils/telemetry.h"

int inflate_soloader(mz_streamp strm, int flush) {
    if (!strm) return MZ_STREAM_ERROR;
    unsigned ao = strm->avail_out;
    uintptr_t no = (uintptr_t)strm->next_out;
    int gpu_mem = (no & 0xFF000000u) == 0x62000000u;   /* vitaGL GPU pool region */
    int bad = (ao > 0x20000000u);                       /* >512MB avail_out = underflow */
    static unsigned s = 0;
    if (s++ < 8) {
        l_info("[INFLATE] in=%p ai=%u out=%p ao=%u tot=%lu fl=%d%s%s",
               (const void *)strm->next_in, strm->avail_in, (void *)strm->next_out,
               ao, (unsigned long)strm->total_out, flush,
               gpu_mem ? " GPUMEM" : "", bad ? " BAD-AVAIL_OUT" : "");
    }
    if (bad) {
        l_error("[INFLATE] refusing inflate: avail_out=%u (underflow) next_out=%p — skipping to avoid memcpy overrun crash",
                ao, (void *)strm->next_out);
        return MZ_STREAM_ERROR;
    }
    int result = mz_inflate(strm, flush);
    if (result < 0 && result != MZ_BUF_ERROR) {
        static unsigned errors;
        if (errors++ < 8)
            telemetry_log("INFLATE_ERROR", "result=%d input=%u output=%u",
                          result, (unsigned)strm->total_in, (unsigned)strm->total_out);
    }
    return result;
}
