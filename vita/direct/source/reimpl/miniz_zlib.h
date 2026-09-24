#ifndef SOLOADER_MINIZ_ZLIB_H
#define SOLOADER_MINIZ_ZLIB_H

#ifndef MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ARCHIVE_APIS
#endif
#ifndef MINIZ_NO_STDIO
#define MINIZ_NO_STDIO
#endif

#include "../../third_party/miniz/miniz.h"

/* Android inflate entry point. Reject implausibly large output capacities
 * before miniz can copy into an undersized destination. Errors remain visible
 * to the caller; optional diagnostics record the failed stream. */
int inflate_soloader(mz_streamp strm, int flush);

#endif
