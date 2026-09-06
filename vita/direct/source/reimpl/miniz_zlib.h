#ifndef SOLOADER_MINIZ_ZLIB_H
#define SOLOADER_MINIZ_ZLIB_H

#ifndef MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ARCHIVE_APIS
#endif
#ifndef MINIZ_NO_STDIO
#define MINIZ_NO_STDIO
#endif

#include "../../third_party/miniz/miniz.h"

/* Diagnostic + guard wrapper around miniz inflate (dynlib maps "inflate" here).
 * PvZ2 crashes inside miniz's tinfl with next_out in GPU memory + an underflowed
 * avail_out (~4GB) -> memcpy overrun. Logs each call's buffers and refuses to run
 * a call whose avail_out is impossibly large, turning the hard crash into a
 * logged graceful failure so we can see exactly which decompress is bad. */
int inflate_soloader(mz_streamp strm, int flush);

#endif
