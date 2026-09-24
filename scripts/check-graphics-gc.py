"""Exercise the production collector barrier with a deliberately delayed worker."""
from pathlib import Path
import subprocess, tempfile
r=Path(__file__).resolve().parents[1]
w=Path(tempfile.mkdtemp(prefix='graphics-gc-',dir=r/'out'))
s=(r/'vita/direct/source/utils/graphics_gc.c').read_text()
s='\n'.join(line for line in s.splitlines() if not line.startswith('#include'))
c=r'''
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
typedef int SceUID; typedef unsigned SceUInt;
typedef struct { unsigned size; int maxCount; } SceKernelSemaInfo;
SceUID gc_mutex[2]={11,12};
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static int available,maximum=4,requests,finished,stop,fail_wait,fail_signal;
static unsigned cursor,expected_exit,unrelated_calls;
static int __real_sceKernelWaitSema(int id,int n,unsigned *timeout) {
    if(id!=11 && id!=12) { ++unrelated_calls;return 73; }
    assert(id==12 && timeout && *timeout==5000000);
    if(fail_wait)return -110;
    pthread_mutex_lock(&lock);
    while(available<n)pthread_cond_wait(&changed,&lock);
    available-=n;
    pthread_mutex_unlock(&lock);return 0;
}
static int __real_sceKernelSignalSema(int id,int n) {
    if(id!=11 && id!=12) { ++unrelated_calls;return 74; }
    if(fail_signal)return -22;
    pthread_mutex_lock(&lock);
    if(id==11)requests+=n;else { available+=n;assert(available<=maximum); }
    pthread_cond_broadcast(&changed);pthread_mutex_unlock(&lock);return 0;
}
static int sceKernelGetSemaInfo(int id,SceKernelSemaInfo *p) { assert(id==12);p->maxCount=maximum;return 0; }
static void telemetry_log(const char *tag,const char *fmt,...) { assert(!strcmp(tag,"FATAL")); }
static void sceKernelExitProcess(int rc) { assert(expected_exit && rc==6);puts("PASS: failed collector stops without returning to GL");exit(0); }
static void sceKernelDelayThread(unsigned us) { assert(0); }
static int __real_sceKernelDelayThread(unsigned us) { ++unrelated_calls;return 75; }
static void *gpu_alloc_mapped_aligned_unsafe(size_t a,size_t n,int t) { return NULL; }
'''+s+r'''
static void *worker(void *unused) {
    pthread_mutex_lock(&lock);
    for(;;) {
        while(!requests && !stop)pthread_cond_wait(&changed,&lock);
        if(stop)break;
        --requests;pthread_mutex_unlock(&lock);
        usleep(50+(finished%7)*17); /* renderer must not race this cursor reset */
        cursor=0;
        pthread_mutex_lock(&lock);++finished;++available;
        pthread_cond_broadcast(&changed);
    }
    pthread_mutex_unlock(&lock);return NULL;
}
int main(int argc,char **argv) {
    if(argc>1 && !atoi(argv[1])) {
        expected_exit=1;
        if(!strcmp(argv[1],"request")) { fail_signal=1;__wrap_sceKernelSignalSema(11,1); }
        else if(!strcmp(argv[1],"completion")) { fail_wait=1;__wrap_sceKernelSignalSema(11,1); }
        else { fail_wait=1;pvz2_gc_wait(12,1,NULL); }
        assert(0);
    }
    assert(pvz2_gc_wait(77,1,NULL)==73);
    assert(__wrap_sceKernelSignalSema(77,1)==74 && unrelated_calls==2);
    assert(__wrap_sceKernelDelayThread(1000000)==75 && unrelated_calls==3);
    maximum=argc>1 ? atoi(argv[1]) : 4;
    {
        available=maximum;requests=finished=stop=0;
        pthread_t t;assert(!pthread_create(&t,NULL,worker,NULL));
        for(int frame=0;frame<1000;frame++) {
            cursor=57;
            assert(!pvz2_gc_wait(12,1,NULL));
            assert(!__wrap_sceKernelSignalSema(11,1));
            assert(cursor==0 && finished==frame+1 && available==maximum);
            cursor=91;usleep(15);assert(cursor==91);
        }
        pthread_mutex_lock(&lock);stop=1;pthread_cond_signal(&changed);pthread_mutex_unlock(&lock);
        assert(!pthread_join(t,NULL));
    }
    puts("PASS: 1000 delayed collection cycles; cursor ownership, exact token balance, unchanged unrelated semaphores");
}
'''
f=w/'test.c';f.write_text(c)
exe=w/'test.exe'
subprocess.run(['gcc','-std=c11','-D_DEFAULT_SOURCE','-O2','-pthread',str(f),'-o',str(exe)],check=True)
for case in [['1'],['2'],['4'],['8'],['request'],['completion'],['wait']]:subprocess.run([str(exe),*case],check=True,timeout=30)
