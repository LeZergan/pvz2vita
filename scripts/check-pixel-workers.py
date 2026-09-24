"""Bounded native CPU checks of the production pixel pool; no game or renderer."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = root / 'vita/direct/source'
work = Path(tempfile.mkdtemp(prefix='pixel-workers-', dir=root / 'out'))
c = r'''
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>
typedef int SceUID;
#define SCE_KERNEL_ERROR_WAIT_TIMEOUT (-10)
typedef struct { pthread_mutex_t lock; pthread_cond_t changed; int count, max, live; } TestSema;
static TestSema semas[8];
static int made, create_fail_at=-1, dropped;
static atomic_int drop_notifications;
static int sceKernelCreateSema(const char *name, int attr, int count, int max, void *opts) {
    if(made==create_fail_at) return -1;
    int id=++made; assert(id<8);
    pthread_mutex_init(&semas[id].lock,NULL);pthread_cond_init(&semas[id].changed,NULL);
    semas[id].count=count;semas[id].max=max;semas[id].live=1;return id;
}
static int sceKernelDeleteSema(int id) {
    assert(semas[id].live);semas[id].live=0;
    pthread_cond_destroy(&semas[id].changed);pthread_mutex_destroy(&semas[id].lock);return 0;
}
static int sceKernelSignalSema(int id,int count) {
    if(atomic_load(&drop_notifications)) return -1;
    TestSema *s=&semas[id];assert(s->live);pthread_mutex_lock(&s->lock);
    if(s->count+count>s->max) {pthread_mutex_unlock(&s->lock);return -1;}
    s->count+=count;pthread_cond_broadcast(&s->changed);pthread_mutex_unlock(&s->lock);return 0;
}
static int sceKernelWaitSema(int id,int count,unsigned *timeout) {
    TestSema *s=&semas[id];assert(s->live && count==1 && timeout);
    struct timespec until;clock_gettime(CLOCK_REALTIME,&until);
    until.tv_nsec+=(long)*timeout*1000;
    until.tv_sec+=until.tv_nsec/1000000000;until.tv_nsec%=1000000000;
    pthread_mutex_lock(&s->lock);
    while(!s->count) {
        int rc=pthread_cond_timedwait(&s->changed,&s->lock,&until);
        if(rc==ETIMEDOUT) {pthread_mutex_unlock(&s->lock);return SCE_KERNEL_ERROR_WAIT_TIMEOUT;}
        assert(!rc);
    }
    --s->count;pthread_mutex_unlock(&s->lock);return 0;
}
static int sceKernelDelayThread(unsigned us) {usleep(us);return 0;}
#include "utils/pixel_workers.c"
static int create_limit, attempts;
static atomic_int hold_start=1;
static struct {void *(*fn)(void *); void *arg;} delayed[3];
static void *late_start(void *arg) {
    unsigned i=(unsigned)(uintptr_t)arg;
    while(atomic_load(&hold_start)) usleep(100);
    return delayed[i].fn(delayed[i].arg);
}
int pthread_create_soloader(pthread_t *t, const pthread_attr_t_bionic *a,
                            void *(*f)(void *), void *v) {
    if (attempts++ >= create_limit) return EAGAIN;
    unsigned i=(unsigned)(uintptr_t)v;delayed[i].fn=f;delayed[i].arg=v;
    return pthread_create(t, NULL, late_start, v);
}
#include "etc1-reference.h"
static pthread_mutex_t reference_lock=PTHREAD_MUTEX_INITIALIZER;
static void check(int w, int h, int mode, int zero) {
    size_t n = mode>=PVZ2_ETC1_RGBA ? (size_t)((w+3)/4)*((h+3)/4)*8 : (size_t)w*h*(mode==PVZ2_RGBA_HALF ? 4 : 1);
    uint8_t *source = malloc(n + 16);
    assert(source);
    for (size_t i = 0; i < n + 16; ++i) source[i] = (uint8_t)(i*37 + i/113);
    if(mode>=PVZ2_ETC1_RGBA) for(size_t i=3;i<n+3;i+=8) {
        if((i/8)&1) {source[i+3]|=2;for(int c=0;c<3;c++)source[i+c]=(16<<3)|(source[i+c]&7);}
        else source[i+3]&=~2;
    }
    const uint8_t *input = zero ? NULL : source + 3; /* unaligned input */
    uint8_t *actual = pvz2_pixels_convert(input, w, h, mode);
    int dw = (mode == PVZ2_ALPHA_RGBA || mode == PVZ2_ETC1_RGBA) ? w : w/2;
    int dh = (mode == PVZ2_ALPHA_RGBA || mode == PVZ2_ETC1_RGBA) ? h : h/2;
    assert(actual);
    size_t bytes=(size_t)dw*dh*4;
    uint8_t *reused=malloc(bytes+16);assert(reused);memset(reused,0xad,bytes+16);
    assert(!pvz2_pixels_convert_into(input,w,h,mode,reused,bytes-1));
    assert(!pvz2_pixels_convert_into(input,w,h,mode,NULL,bytes));
    assert(pvz2_pixels_convert_into(input,w,h,mode,reused,bytes));
    assert(!memcmp(reused,actual,bytes));
    for(unsigned i=0;i<16;i++)assert(reused[bytes+i]==0xad);
    free(reused);
    uint8_t *reference=NULL;
    if(mode>=PVZ2_ETC1_RGBA) {
        reference=malloc((size_t)w*h*4);assert(reference);
        pthread_mutex_lock(&reference_lock);
        memcpy(reference,reference_decode(input,w,h),(size_t)w*h*4);
        pthread_mutex_unlock(&reference_lock);
    }
    for (int y=0; y<dh; ++y) for (int x=0; x<dw; ++x) {
        for (int ch=0; ch<4; ++ch) {
            unsigned expected = 255;
            if (mode >= PVZ2_ETC1_RGBA) {
                size_t i=((size_t)y*(mode==PVZ2_ETC1_HALF?2:1)*w+x*(mode==PVZ2_ETC1_HALF?2:1))*4+ch;
                expected=mode==PVZ2_ETC1_RGBA ? reference[i] :
                    (reference[i]+reference[i+4]+reference[i+w*4]+reference[i+w*4+4])/4;
            } else if (mode == PVZ2_RGBA_HALF) {
                size_t i=((size_t)y*2*w+x*2)*4+ch;
                expected=(input[i]+input[i+4]+input[i+w*4]+input[i+w*4+4])/4;
            } else if(ch == 3) {
                expected=0;
                if(input) {
                    size_t i=(size_t)y*(mode==PVZ2_ALPHA_HALF ? 2 : 1)*w+
                             x*(mode==PVZ2_ALPHA_HALF ? 2 : 1);
                    expected=mode==PVZ2_ALPHA_RGBA ? input[i] :
                        (input[i]+input[i+1]+input[i+w]+input[i+w+1])/4;
                }
            }
            assert(actual[((size_t)y*dw+x)*4+ch] == expected);
        }
    }
    free(reference); free(actual); free(source); /* pool must retain neither pointer */
}
static void *caller(void *arg) {
    for(int i=0;i<8;++i) check(517,515,(int)(uintptr_t)arg,0);
    return NULL;
}
static void *release_delayed_helpers(void *arg) {
    /* No helper may start until the caller has claimed every strip, including
     * the final out-of-range claim. Unlike fixed slices, no rows are stranded.
     * Do not inspect pixels here: their publication is the completion barrier. */
    while(!atomic_load(&generation) || atomic_load(&next_row)<=1024) usleep(100);
    assert(atomic_load(&pending)==worker_count);
    atomic_store(&hold_start,0);
    return NULL;
}
int main(int argc, char **argv) {
    create_limit=atoi(argv[1]);
    if(argc>2) create_fail_at=atoi(argv[2]);
    check(3,3,PVZ2_ALPHA_HALF,0); /* valid before pool initialization */
    pvz2_pixels_init(3);
    unsigned expected=create_limit;
    if(create_fail_at>=0 && expected>(unsigned)(create_fail_at ? create_fail_at-1 : 0))
        expected=create_fail_at ? create_fail_at-1 : 0;
    assert(worker_count == expected);
    pvz2_pixels_init(3); /* no duplicate pool */
    pthread_t release;
    if(worker_count) assert(!pthread_create(&release,NULL,release_delayed_helpers,NULL));
    check(1024,1024,PVZ2_ALPHA_RGBA,0);
    if(worker_count) {
        assert(!pthread_join(release,NULL));
        puts("PASS: caller drains every strip with all helpers delayed; completion still waits for helper acknowledgment");
    } else atomic_store(&hold_start,0);
    assert(!pvz2_pixels_convert(NULL,2,2,PVZ2_RGBA_HALF));
    assert(!pvz2_pixels_convert(NULL,1,1024,PVZ2_ALPHA_HALF));
    assert(!pvz2_pixels_convert(NULL,-1,1,PVZ2_ALPHA_RGBA));
    assert(!pvz2_pixels_convert(NULL,1,1,9));
    for(int m=0;m<5;++m) {
        check(2,2,m,0); check(7,9,m,0); check(1025,513,m,0);
        if(m==1 || m==2) { check(5,3,m,1); check(1024,1024,m,1); }
    }
    check(1024,1,PVZ2_ALPHA_RGBA,0);
    check(1,1,PVZ2_ETC1_RGBA,0);
    pthread_t callers[5];
    for(int i=0;i<5;++i) assert(!pthread_create(&callers[i],NULL,caller,(void *)(uintptr_t)i));
    for(int i=0;i<5;++i) assert(!pthread_join(callers[i],NULL));
    /* A saturated/stale or lost notification must never own completion.
     * Suppress EVERY start and done notification, including repeated jobs.
     * Polling must still complete all rows before the buffer is freed/reused. */
    atomic_store(&drop_notifications,1);
    for(int i=0;i<4;++i) check(1024,1024,i%5,0);
    atomic_store(&drop_notifications,0);
    for(int i=0;i<40;++i) check(517,515,i%5,0);
    assert(jobs>30);
    if(worker_count) assert(parallel_jobs && worker_pixels && caller_pixels);
    else assert(!parallel_jobs && !worker_pixels);
    char stats[200]; pvz2_pixels_format_stats(stats,sizeof(stats)); puts(stats);
    assert(!jobs && !parallel_jobs && !worker_pixels && !caller_pixels);
    puts("PASS: RGBA/alpha and fused ETC1 match independent serial reference; odd/unaligned/partial blocks, output capacity/guards, concurrent callers and buffer lifetime");
    puts("PASS: all start/done notifications dropped, stale/coalesced tokens, repeated jobs and semaphore allocation failure fallback");
}
'''
(work / 'check.c').write_text(c, encoding='utf-8')
exe = work / 'check.exe'
subprocess.run(['gcc','-std=gnu11','-O2','-static','-pthread','-DPVZ2_PIXEL_HOST_TEST',
                '-I'+str(src),'-I'+str(root/'scripts/fixtures'),str(work/'check.c'),'-o',str(exe)], check=True, timeout=30)
results=[]
for workers,fail in [(0,-1),(1,-1),(2,-1),(3,-1),(3,0),(3,1),(3,2),(3,3)]:
    run=subprocess.run([str(exe),str(workers),str(fail)],check=True,capture_output=True,text=True,timeout=8)
    results.append(run.stdout)
    print(run.stdout.strip())
(work/'result.txt').write_text(''.join(results),encoding='utf-8')
print(work)
