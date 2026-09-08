/* MIT license; see the project's LICENSE. */
#include "reimpl/bionic_time.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <utime.h>
#include <psp2/rtc.h>
#include "utils/logger.h"

_Static_assert(offsetof(bionic_tm, tm_gmtoff) == 36, "Android timezone ABI");
_Static_assert(offsetof(bionic_tm, tm_zone) == 40, "Android zone name ABI");
#if defined(__arm__)
_Static_assert(sizeof(bionic_tm) == 44, "Android tm size");
_Static_assert(sizeof(time_t) == 4, "Check time/gettimeofday/utime imports if SDK changes");
_Static_assert(sizeof(struct timeval) == 8, "Android ARMv7 timeval");
_Static_assert(sizeof(struct timespec) == 8, "Android ARMv7 timespec");
_Static_assert(sizeof(struct utimbuf) == 8, "Android ARMv7 utime arguments");
#endif

static __thread bionic_tm result_tm;
static __thread char result_string[26];
static __thread char result_zone[16];
static int last_offset, rtc_warning;
static const int month_start[] = {0,31,59,90,120,151,181,212,243,273,304,334};

static int64_t floor_div(int64_t a, int64_t b) {
    return a / b - (a % b < 0);
}
static int leap(int64_t year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}
static int64_t year_start(int64_t year) {
    --year;
    return year * 365 + floor_div(year,4) - floor_div(year,100) + floor_div(year,400) - 719162;
}

/* Vita supplies a configured UTC offset, not an IANA historical timezone
 * database. Query that offset using today's valid RTC tick: converting an
 * epoch-adjacent local date through sceRtcGetTime_t can return NULL (RC6).
 * Re-querying also picks up timezone changes after suspend/resume. */
static int local_offset(void) {
    SceRtcTick utc, local;
    if (sceRtcGetCurrentTick(&utc) >= 0 &&
        sceRtcConvertUtcToLocalTime(&utc, &local) >= 0) {
        uint64_t delta = local.tick >= utc.tick ? local.tick-utc.tick : utc.tick-local.tick;
        if (delta <= UINT64_C(86400000000) && delta % 1000000 == 0) {
            int offset = (int)(delta / 1000000);
            if (local.tick < utc.tick) offset = -offset;
            __atomic_store_n(&last_offset, offset, __ATOMIC_RELAXED);
            return offset;
        }
    }
    if (!__atomic_exchange_n(&rtc_warning, 1, __ATOMIC_RELAXED))
        l_warn("TIME: RTC timezone unavailable; retaining last offset (UTC until first success)");
    return __atomic_load_n(&last_offset, __ATOMIC_RELAXED);
}

static const char *zone_name(int offset, char *out, size_t size) {
    if (!offset) return "UTC";
    unsigned value = offset < 0 ? -(int64_t)offset : offset;
    snprintf(out, size, "UTC%c%02u:%02u", offset < 0 ? '-' : '+', value/3600, value/60%60);
    return out;
}

static bionic_tm *split_time(int64_t seconds, int offset, bionic_tm *out) {
    if (!out) { errno = EINVAL; return NULL; }
    /* Bound before adding the zone or doing calendar arithmetic. */
    if (seconds < year_start((int64_t)INT32_MIN+1900)*86400-offset ||
        seconds >= year_start((int64_t)INT32_MAX+1901)*86400-offset) {
        errno = EOVERFLOW; return NULL;
    }
    seconds += offset;
    int64_t days = floor_div(seconds,86400), rem = seconds-days*86400;
    int64_t lo = floor_div(days+719162,146097)*400+1, hi = lo+400;
    while (lo+1 < hi) {
        int64_t mid = lo+(hi-lo)/2;
        if (year_start(mid) <= days) lo = mid; else hi = mid;
    }
    int yday = (int)(days-year_start(lo)), mon = 11;
    while (month_start[mon]+(mon > 1 && leap(lo)) > yday) --mon;
    bionic_tm value = {0};
    value.tm_sec = rem%60; value.tm_min = rem/60%60; value.tm_hour = rem/3600;
    value.tm_year = (int32_t)(lo-1900); value.tm_mon = mon;
    value.tm_mday = yday-month_start[mon]-(mon > 1 && leap(lo))+1;
    value.tm_yday = yday; value.tm_wday = (days+4)%7;
    if (value.tm_wday < 0) value.tm_wday += 7;
    /* The RTC offset already includes the console's daylight adjustment. */
    value.tm_isdst = 0; value.tm_gmtoff = offset;
    value.tm_zone = zone_name(offset, result_zone, sizeof(result_zone));
    *out = value;
    return out;
}

bionic_tm *bionic_gmtime_r(const int32_t *t, bionic_tm *out) {
    if (!t) { errno = EINVAL; return NULL; }
    return split_time(*t, 0, out);
}
bionic_tm *bionic_localtime_r(const int32_t *t, bionic_tm *out) {
    if (!t) { errno = EINVAL; return NULL; }
    return split_time(*t, local_offset(), out);
}
bionic_tm *bionic_gmtime(const int32_t *t) { return bionic_gmtime_r(t, &result_tm); }
bionic_tm *bionic_localtime(const int32_t *t) { return bionic_localtime_r(t, &result_tm); }
bionic_tm *bionic_gmtime64(const int64_t *t) {
    if (!t) { errno = EINVAL; return NULL; }
    return split_time(*t, 0, &result_tm);
}
bionic_tm *bionic_localtime64(const int64_t *t) {
    if (!t) { errno = EINVAL; return NULL; }
    return split_time(*t, local_offset(), &result_tm);
}

/* Normalize every signed int field in 64 bits before indexing month tables.
 * This covers December+1, day zero, negative seconds, and malformed dates
 * without looping billions of times or indexing outside a calendar array. */
static int64_t join_time(const bionic_tm *t) {
    int64_t year = (int64_t)t->tm_year+1900+floor_div(t->tm_mon,12);
    int mon = t->tm_mon-floor_div(t->tm_mon,12)*12;
    int64_t days = year_start(year)+month_start[mon]+(mon > 1 && leap(year))+(int64_t)t->tm_mday-1;
    return days*86400+(int64_t)t->tm_hour*3600+(int64_t)t->tm_min*60+t->tm_sec;
}
static int64_t make_time(bionic_tm *t, int local, int narrow) {
    if (!t) { errno = EINVAL; return -1; }
    int offset = local ? local_offset() : 0;
    int64_t seconds = join_time(t)-offset;
    if (narrow && (seconds < INT32_MIN || seconds > INT32_MAX)) {
        errno = EOVERFLOW; return -1;
    }
    return split_time(seconds,offset,t) ? seconds : -1;
}
int32_t bionic_mktime(bionic_tm *t) { return (int32_t)make_time(t,1,1); }
int32_t bionic_timegm(bionic_tm *t) { return (int32_t)make_time(t,0,1); }
int64_t bionic_mktime64(bionic_tm *t) { return make_time(t,1,0); }

static void native_tm(const bionic_tm *from, struct tm *to) {
    memset(to,0,sizeof(*to));
    to->tm_sec=from->tm_sec; to->tm_min=from->tm_min; to->tm_hour=from->tm_hour;
    to->tm_mday=from->tm_mday; to->tm_mon=from->tm_mon; to->tm_year=from->tm_year;
    to->tm_wday=from->tm_wday; to->tm_yday=from->tm_yday; to->tm_isdst=from->tm_isdst;
}
static void from_native(const struct tm *from, bionic_tm *to) {
    to->tm_sec=from->tm_sec; to->tm_min=from->tm_min; to->tm_hour=from->tm_hour;
    to->tm_mday=from->tm_mday; to->tm_mon=from->tm_mon; to->tm_year=from->tm_year;
    to->tm_wday=from->tm_wday; to->tm_yday=from->tm_yday; to->tm_isdst=from->tm_isdst;
}

char *bionic_asctime_r(const bionic_tm *t, char *out) {
    static const char *days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char *months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    if (!t || !out || t->tm_wday < 0 || t->tm_wday > 6 || t->tm_mon < 0 || t->tm_mon > 11 ||
        t->tm_mday < 1 || t->tm_mday > 31 || t->tm_hour < 0 || t->tm_hour > 23 ||
        t->tm_min < 0 || t->tm_min > 59 || t->tm_sec < 0 || t->tm_sec > 60) {
        errno=EINVAL; return NULL;
    }
    if (t->tm_year < -1900 || t->tm_year > 8099) { errno=EOVERFLOW; return NULL; }
    snprintf(out,26,"%s %s %2d %02d:%02d:%02d %04d\n",days[t->tm_wday],months[t->tm_mon],
             t->tm_mday,t->tm_hour,t->tm_min,t->tm_sec,t->tm_year+1900);
    return out;
}
char *bionic_asctime(const bionic_tm *t) { return bionic_asctime_r(t,result_string); }
char *bionic_ctime_r(const int32_t *t, char *out) {
    bionic_tm value;
    return bionic_localtime_r(t,&value) ? bionic_asctime_r(&value,out) : NULL;
}
char *bionic_ctime(const int32_t *t) { return bionic_ctime_r(t,result_string); }

size_t bionic_strftime(char *out, size_t size, const char *format, const bionic_tm *t) {
    if (!out || !size || !format || !t) return 0;
    /* Newlib indexes its locale's weekday/month arrays with these values. */
    if (t->tm_mon < 0 || t->tm_mon > 11 || t->tm_wday < 0 || t->tm_wday > 6 ||
        t->tm_year < -1900 || t->tm_year > 8099) { out[0]=0; return 0; }
    struct tm native; native_tm(t,&native);
    size_t used=0; out[0]=0;
    while (*format) {
        char value[128], spec[4]={'%',0,0,0};
        const char *piece=value;
        if (*format != '%') { value[0]=*format++; value[1]=0; }
        else {
            ++format; int i=1;
            if (*format == 'E' || *format == 'O') spec[i++]=*format++;
            char code=*format;
            if (!code) return 0;
            spec[i]=*format++;
            if (code == 'z' || code == 'Z') {
                int offset=t->tm_gmtoff;
                if (offset < -86400 || offset > 86400) return 0;
                if (t->tm_isdst < 0) value[0]=0;
                else if (code == 'Z') piece=zone_name(offset,value,sizeof(value));
                else {
                    unsigned n=offset < 0 ? -offset : offset;
                    snprintf(value,sizeof(value),"%c%02u%02u",offset < 0 ? '-' : '+',n/3600,n/60%60);
                }
            } else if (code == 's') {
                snprintf(value,sizeof(value),"%lld",(long long)(join_time(t)-t->tm_gmtoff));
            } else if (!strftime(value,sizeof(value),spec,&native)) return 0;
        }
        size_t n=strlen(piece);
        if (n >= size-used) return 0;
        memcpy(out+used,piece,n); used+=n; out[used]=0;
    }
    return used;
}

static const char *special_time_spec(const char *format) {
    while (*format) {
        if (*format++ != '%') continue;
        const char *start=format-1;
        if (*format == 'E' || *format == 'O') ++format;
        if (*format == 'z' || *format == 'Z' || *format == 's') return start;
        if (*format) ++format;
    }
    return format;
}

static const char *parse_offset(const char *s, int *offset, int named) {
    int prefix=0;
    if (named && (!strncmp(s,"UTC",3) || !strncmp(s,"GMT",3))) { s+=3; prefix=1; }
    if (*s == 'Z') { *offset=0; return s+1; }
    if (prefix && *s != '+' && *s != '-') { *offset=0; return s; }
    if (*s != '+' && *s != '-') return NULL;
    int sign=*s++ == '-' ? -1 : 1;
    if (!isdigit((unsigned char)s[0]) || !isdigit((unsigned char)s[1])) return NULL;
    int hours=(s[0]-'0')*10+s[1]-'0'; s+=2;
    if (*s == ':') ++s;
    if (!isdigit((unsigned char)s[0]) || !isdigit((unsigned char)s[1])) return NULL;
    int minutes=(s[0]-'0')*10+s[1]-'0'; s+=2;
    if (hours > 23 || minutes > 59) return NULL;
    *offset=sign*(hours*3600+minutes*60);
    return s;
}

char *bionic_strptime(const char *input, const char *format, bionic_tm *out) {
    if (!input || !format || !out) { errno=EINVAL; return NULL; }
    struct tm native; native_tm(out,&native);
    int offset=0;
    const char *special=special_time_spec(format);
    if (!*special) {
        /* Keep century/year and AM/PM parser state together for native formats. */
        char *end=strptime(input,format,&native);
        if (!end) return NULL;
        from_native(&native,out); out->tm_gmtoff=0; out->tm_zone="UTC";
        return end;
    }
    /* This SDK does not implement %z/%Z/%s. Parse those at the Android boundary;
     * do not send %s through the SDK's epoch-limited localtime path. */
    char *segment=malloc(strlen(format)+1);
    if (!segment) { errno=ENOMEM; return NULL; }
    while (*format) {
        special=special_time_spec(format);
        size_t n=(size_t)(special-format);
        if (n) {
            memcpy(segment,format,n); segment[n]=0;
            input=strptime(input,segment,&native);
            if (!input) goto fail;
        }
        format=special;
        if (!*format) break;
        ++format;
        if (*format == 'E' || *format == 'O') ++format;
        char code=*format++;
        if (code == 's') {
            char *end;
            int saved_errno=errno; errno=0;
            int64_t seconds=strtoll(input,&end,10);
            if (end == input || errno == ERANGE) goto fail;
            errno=saved_errno;
            bionic_tm value;
            offset=local_offset();
            if (!split_time(seconds,offset,&value)) goto fail;
            native_tm(&value,&native); input=end;
        } else {
            input=parse_offset(input,&offset,code == 'Z');
            if (!input) goto fail;
            native.tm_isdst=0;
        }
    }
    from_native(&native,out); out->tm_gmtoff=offset;
    out->tm_zone=zone_name(offset,result_zone,sizeof(result_zone));
    free(segment);
    return (char *)input;
fail:
    free(segment);
    return NULL;
}
