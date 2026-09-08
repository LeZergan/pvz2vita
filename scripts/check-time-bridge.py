"""Exercise the production Android time bridge with injected RTC responses.

No Vita/game boot, UI, or user data. Python's datetime is the independent
calendar oracle. The Windows parser adapter only tests the ABI copy; the
optional ARM check uses the actual linked newlib parser.
"""
from pathlib import Path
import concurrent.futures, ctypes as C, datetime as D, os, random, shutil, subprocess, tempfile

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/'vita/direct/source'
work=Path(tempfile.mkdtemp(prefix='time-bridge-',dir=ROOT/'out'))
(work/'psp2').mkdir()
(work/'psp2/rtc.h').write_text('''#include <stdint.h>
typedef struct { uint64_t tick; } SceRtcTick;
int sceRtcGetCurrentTick(SceRtcTick *);
int sceRtcConvertUtcToLocalTime(const SceRtcTick *, SceRtcTick *);
''')
adapter=r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <psp2/rtc.h>
static int zone, failure, warnings;
void test_rtc(int offset, int fail) { zone=offset; failure=fail; }
int test_warnings(void) { return warnings; }
int sceRtcGetCurrentTick(SceRtcTick *t) {
    if(failure==1) return -1;
    t->tick=63924556800000000ULL; return 0;
}
int sceRtcConvertUtcToLocalTime(const SceRtcTick *a,SceRtcTick *b) {
    if(failure==2) return -1;
    b->tick=a->tick+(int64_t)zone*1000000; return 0;
}
void _log_print(int level,const char *format,...) { ++warnings; }
'''
if os.name=='nt':
    adapter+=r'''
char *strptime(const char *s,const char *fmt,struct tm *t) {
    int y,m,d,h,mi,se,n=0;
    if(sscanf(s,"%d-%d-%d %d:%d:%d%n",&y,&m,&d,&h,&mi,&se,&n)!=6 || !n) return NULL;
    t->tm_year=y-1900;t->tm_mon=m-1;t->tm_mday=d;
    t->tm_hour=h;t->tm_min=mi;t->tm_sec=se;
    if(*fmt && fmt[strlen(fmt)-1]==' ') while(s[n]==' ' || s[n]=='\t') ++n;
    return (char*)s+n;
}
'''
(work/'adapter.c').write_text(adapter)
(work/'parser.h').write_text('#include <time.h>\nchar *strptime(const char *,const char *,struct tm *);\n')
libpath=work/('check.dll' if os.name=='nt' else 'check.so')
subprocess.run(['gcc','-shared','-fPIC','-O2','-std=gnu11','-DDEBUG_SOLOADER','-D_XOPEN_SOURCE=700',
                '-Wall','-Wextra','-Werror','-Wno-unused-parameter','-include',str(work/'parser.h'),
                '-I',str(work),'-I',str(SRC),str(SRC/'reimpl/bionic_time.c'),str(work/'adapter.c'),
                '-o',str(libpath)],check=True,timeout=30)
dll_dir=os.add_dll_directory(str(Path(shutil.which('gcc')).parent)) if os.name=='nt' else None
lib=C.CDLL(str(libpath))
class TM(C.Structure):
    _fields_=[(f'tm_{x}',C.c_int32) for x in 'sec min hour mday mon year wday yday isdst gmtoff'.split()]+[('tm_zone',C.c_void_p)]
class Guard(C.Structure):
    _fields_=[('before',C.c_uint64*2),('value',TM),('after',C.c_uint64*2)]
for name in ('gmtime_r','localtime_r'):
    f=getattr(lib,'bionic_'+name);f.argtypes=[C.POINTER(C.c_int32),C.POINTER(TM)];f.restype=C.POINTER(TM)
for name in ('gmtime','localtime','gmtime64','localtime64'):
    f=getattr(lib,'bionic_'+name);f.argtypes=[C.POINTER(C.c_int64 if name.endswith('64') else C.c_int32)];f.restype=C.POINTER(TM)
for name in ('mktime','timegm','mktime64'):
    f=getattr(lib,'bionic_'+name);f.argtypes=[C.POINTER(TM)];f.restype=C.c_int64 if name.endswith('64') else C.c_int32
lib.bionic_strftime.argtypes=[C.c_char_p,C.c_size_t,C.c_char_p,C.POINTER(TM)];lib.bionic_strftime.restype=C.c_size_t
lib.bionic_strptime.argtypes=[C.c_char_p,C.c_char_p,C.POINTER(TM)];lib.bionic_strptime.restype=C.c_void_p
lib.bionic_asctime_r.argtypes=[C.POINTER(TM),C.c_void_p];lib.bionic_asctime_r.restype=C.c_void_p
lib.bionic_ctime.argtypes=[C.POINTER(C.c_int32)];lib.bionic_ctime.restype=C.c_char_p
epoch=D.datetime(1970,1,1)
def verify(t,seconds,offset):
    d=epoch+D.timedelta(seconds=seconds+offset)
    expected=(d.second,d.minute,d.hour,d.day,d.month-1,d.year-1900,(d.weekday()+1)%7,d.timetuple().tm_yday-1,0,offset)
    got=tuple(getattr(t,n) for n,_ in TM._fields_[:10])
    assert got==expected,(seconds,offset,got,expected)
    assert t.tm_zone and C.string_at(t.tm_zone).startswith(b'UTC')

lib.test_rtc(0,1)
t=C.c_int32(6)
verify(lib.bionic_localtime(C.byref(t)).contents,6,0)
assert lib.test_warnings()==1
values=[-(2**31),-86401,-86400,-1,0,1,6,86399,86400,951782400,2147483647]
rng=random.Random(452)
values += [rng.randint(-2**31,2**31-1) for _ in range(1500)]
for offset in (-43200,-39600,-14400,-12600,-3600,0,3600,10800,19800,20700,34200,45900,50400):
    lib.test_rtc(offset,0)
    for seconds in values:
        g=Guard();g.before[:]=[0x12345678abcdef01]*2;g.after[:]=[0xfedcba0987654321]*2
        t=C.c_int32(seconds)
        assert lib.bionic_localtime_r(C.byref(t),C.byref(g.value))
        verify(g.value,seconds,offset)
        assert lib.bionic_mktime(C.byref(g.value))==seconds
        assert lib.bionic_gmtime_r(C.byref(t),C.byref(g.value))
        verify(g.value,seconds,0)
        assert lib.bionic_timegm(C.byref(g.value))==seconds
        assert list(g.before)==[0x12345678abcdef01]*2 and list(g.after)==[0xfedcba0987654321]*2
print(f'PASS: {len(values)*13} timezone/calendar cases and local/UTC round trips, with buffer canaries.')

lib.test_rtc(-14400,0);t=C.c_int32(6)
v=lib.bionic_localtime(C.byref(t)).contents
verify(v,6,-14400)
buf=C.create_string_buffer(128)
fmt=b'%Y-%m-%d %H:%M:%S %z %Z %s %%'
assert lib.bionic_strftime(buf,len(buf),fmt,C.byref(v))
assert buf.value==b'1969-12-31 20:00:06 -0400 UTC-04:00 6 %',buf.value
assert lib.bionic_ctime(C.byref(t))==b'Wed Dec 31 20:00:06 1969\n'
small=C.create_string_buffer(b'xxxx')
assert lib.bionic_strftime(small,2,b'%Y',C.byref(v))==0 and small.raw[2:]==b'xx\0'
for failure in (1,2):
    lib.test_rtc(0,failure)
    for _ in range(100): verify(lib.bionic_localtime(C.byref(t)).contents,6,-14400)
assert lib.test_warnings()==1
lib.test_rtc(1000000,0)
verify(lib.bionic_localtime(C.byref(t)).contents,6,-14400)
lib.test_rtc(20700,0)
verify(lib.bionic_localtime(C.byref(t)).contents,6,20700)

lib.test_rtc(0,0)
for date in (D.datetime(1,1,1),D.datetime(1600,2,29),D.datetime(1900,3,1),D.datetime(2000,2,29),D.datetime(2100,3,1),D.datetime(9999,12,31)):
    seconds=int((date-epoch).total_seconds());wide=C.c_int64(seconds)
    v=lib.bionic_gmtime64(C.byref(wide)).contents;verify(v,seconds,0)
    assert lib.bionic_mktime64(C.byref(v))==seconds
for seconds in (-2**63,2**63-1):
    wide=C.c_int64(seconds);assert not lib.bionic_gmtime64(C.byref(wide))
for month,day,hour,minute,second,expected in [(12,1,0,0,0,D.datetime(2025,1,1)),(-1,0,0,0,0,D.datetime(2023,11,30)),(1,28,25,61,-1,D.datetime(2024,2,29,2,0,59))]:
    v=TM(tm_year=124,tm_mon=month,tm_mday=day,tm_hour=hour,tm_min=minute,tm_sec=second)
    seconds=int((expected-epoch).total_seconds())
    assert lib.bionic_mktime(C.byref(v))==seconds;verify(v,seconds,0)
for field,_ in TM._fields_[:6]:
    for extreme in (-2**31,2**31-1):
        v=TM(tm_year=124,tm_mon=0,tm_mday=1);setattr(v,field,extreme)
        lib.bionic_mktime(C.byref(v)) # bounded normalization/error, no hangs/OOB
assert not lib.bionic_gmtime(None) and not lib.bionic_gmtime_r(C.byref(t),None)
assert lib.bionic_mktime(None)==-1
v=TM(tm_mon=99);assert not lib.bionic_asctime_r(C.byref(v),buf)
v=TM();s=C.create_string_buffer(b'2024-02-29 13:14:15 trailing')
end=lib.bionic_strptime(s,b'%Y-%m-%d %H:%M:%S',C.byref(v))
assert end==C.addressof(s)+19 and (v.tm_year,v.tm_mon,v.tm_mday)==(124,1,29)
before=bytes(v);assert not lib.bionic_strptime(b'garbage',b'%Y',C.byref(v));assert bytes(v)==before
for text,fmt,offset in [(b'2024-02-29 13:14:15 +0545',b'%Y-%m-%d %H:%M:%S %z',20700),
                        (b'2024-02-29 13:14:15 UTC-04:00',b'%Y-%m-%d %H:%M:%S %Z',-14400),
                        (b'6',b'%s',0)]:
    s=C.create_string_buffer(text)
    assert lib.bionic_strptime(s,fmt,C.byref(v))==C.addressof(s)+len(text)
    assert v.tm_gmtoff==offset
for bad in (b'+',b'+0',b'+00',b'+00:',b'+000',b'+2460',b'-9999'):
    assert not lib.bionic_strptime(bad,b'%z',C.byref(v))
assert not lib.bionic_strptime(b'9223372036854775808',b'%s',C.byref(v))
print('PASS: exact epoch-6 crash scenario; RTC failure/recovery and bounded warning; formatting/parsing; leap days; 64-bit limits and malformed fields.')

def thread_case(i):
    stamp=C.c_int32(86400*i+6)
    for _ in range(200):
        ptr=lib.bionic_localtime(C.byref(stamp))
        verify(ptr.contents,stamp.value,0)
        assert lib.bionic_mktime(ptr)==stamp.value
with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:list(pool.map(thread_case,range(16)))
print('PASS: eight concurrent callers retain independent tm buffers.')
print(work)
