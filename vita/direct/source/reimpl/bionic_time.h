/* Android ARMv7 time ABI. MIT license; see the project's LICENSE. */
#ifndef PVZ2_BIONIC_TIME_H
#define PVZ2_BIONIC_TIME_H
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

/* Newlib's struct tm stops after tm_isdst. Never return it to Android. */
typedef struct {
    int32_t tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year;
    int32_t tm_wday, tm_yday, tm_isdst, tm_gmtoff;
    const char *tm_zone;
} bionic_tm;

bionic_tm *bionic_gmtime_r(const int32_t *, bionic_tm *);
bionic_tm *bionic_localtime_r(const int32_t *, bionic_tm *);
bionic_tm *bionic_gmtime(const int32_t *);
bionic_tm *bionic_localtime(const int32_t *);
bionic_tm *bionic_gmtime64(const int64_t *);
bionic_tm *bionic_localtime64(const int64_t *);
int32_t bionic_mktime(bionic_tm *);
int32_t bionic_timegm(bionic_tm *);
int64_t bionic_mktime64(bionic_tm *);
char *bionic_asctime_r(const bionic_tm *, char *);
char *bionic_asctime(const bionic_tm *);
char *bionic_ctime_r(const int32_t *, char *);
char *bionic_ctime(const int32_t *);
size_t bionic_strftime(char *, size_t, const char *, const bionic_tm *);
char *bionic_strptime(const char *, const char *, bionic_tm *);
#endif
