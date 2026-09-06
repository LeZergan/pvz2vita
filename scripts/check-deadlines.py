"""Production deadline/clock checks using 32-bit Vita time fields; no device/UI."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]; src=r/'vita/direct/source'
w=Path(tempfile.mkdtemp(prefix='deadlines-',dir=r/'out'))
p=(src/'reimpl/pthr.c').read_text(encoding='utf-8'); s=(src/'reimpl/sys.c').read_text(encoding='utf-8')
c=r'''
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <assert.h>
#include <sys/time.h>
typedef unsigned uint;
typedef int clockid_t;
struct vita_timespec { int32_t tv_sec, tv_nsec; };
#define timespec vita_timespec
#define SCE_KERNEL_ERROR_WAIT_TIMEOUT 0x80028005u
#define SCE_KERNEL_ERROR_WAIT_CANCEL 0x80028007u
static int64_t now_us=1788652800123456LL;
static unsigned waits, first_block, blocks;
static int token, result_on_block, probe_error;
static int sceKernelWaitSema(int id,int n,uint *timeout) {
    ++waits;
    if (probe_error) return probe_error;
    if (token) { --token; return 0; }
    if (*timeout) {
        if(!blocks++) first_block=*timeout;
        now_us+=*timeout;
        if (result_on_block) return result_on_block;
    }
    return (int)SCE_KERNEL_ERROR_WAIT_TIMEOUT;
}
static int fake_gettimeofday(struct timeval *t,void *tz) {
    t->tv_sec=now_us/1000000; t->tv_usec=now_us%1000000; return 0;
}
#define gettimeofday fake_gettimeofday
typedef struct { uint64_t tick; } SceRtcTick;
static const uint64_t __epoch=0;
static uint64_t sceKernelGetSystemTimeWide(void) { return now_us; }
static int sceRtcGetCurrentTick(SceRtcTick *t) { t->tick=now_us; return 0; }
'''
c+=p[p.index('int sem_timedwait_soloader'):p.index('int sem_trywait_soloader')]
a=s.index('#define BIONIC_CLOCK_REALTIME '); b=s.index('\n',s.index('#define BIONIC_CLOCK_TAI ',a))
c+=s[a:b]+'\n'
c+=s[s.index('int clock_gettime_soloader'):s.index('int clock_getres_soloader')]
c+=r'''
int main(void) {
    int sem=1;
    struct timespec deadline={1788652800,173456000}; /* Exactly 50ms ahead. */
    assert(sem_timedwait_soloader(&sem,&deadline)==-1 && errno==ETIMEDOUT);
    assert(first_block==50000 && blocks==1 && waits==2);
    waits=blocks=0; deadline.tv_sec=1788652800; deadline.tv_nsec=173455000;
    assert(sem_timedwait_soloader(&sem,&deadline)==-1 && errno==ETIMEDOUT && waits==1);
    token=1; assert(!sem_timedwait_soloader(&sem,NULL));
    deadline.tv_nsec=1000000000;
    assert(sem_timedwait_soloader(&sem,&deadline)==-1 && errno==EINVAL);
    deadline.tv_nsec=173457001; blocks=0;
    assert(sem_timedwait_soloader(&sem,&deadline)==-1 && first_block==2); /* Round up microseconds. */
    deadline.tv_sec+=5000; deadline.tv_nsec=0; blocks=0;
    assert(sem_timedwait_soloader(&sem,&deadline)==-1 && errno==ETIMEDOUT);
    assert(first_block==UINT32_MAX && blocks==2); /* Long waits must not expire at the clamp. */
    deadline.tv_sec+=1; result_on_block=(int)SCE_KERNEL_ERROR_WAIT_CANCEL;
    assert(sem_timedwait_soloader(&sem,&deadline)==-1 && errno==EINTR);
    now_us=1788652800123456LL;
    struct timespec t;
    assert(!clock_gettime_soloader(0,&t) && t.tv_sec==1788652800 && t.tv_nsec==123456000);
    assert(!clock_gettime_soloader(1,&t) && t.tv_nsec==123456000);
    assert(clock_gettime_soloader(1234,&t)==-1 && errno==EINVAL);
    assert(clock_gettime_soloader(1,NULL)==-1 && errno==EFAULT);
    puts("PASS: 32-bit epoch deadline (50ms, not 1ms); immediate/expired/invalid/rounded/long waits; interruption; normalized realtime/monotonic nanoseconds and errors");
}
'''
(w/'check.c').write_text(c,encoding='utf-8');e=w/'check.exe'
subprocess.run(['gcc','-O2','-static',str(w/'check.c'),'-o',str(e)],check=True,timeout=30)
run=subprocess.run([str(e)],check=True,capture_output=True,text=True,timeout=3)
(w/'result.txt').write_text(run.stdout,encoding='utf-8'); print(run.stdout.strip()); print(w)
