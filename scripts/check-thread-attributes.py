"""Production Android thread attribute/query checks with mocked Vita geometry."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1];src=r/'vita/direct/source'
w=Path(tempfile.mkdtemp(prefix='thread-attributes-',dir=r/'out'))
s=(src/'reimpl/pthr.c').read_text(encoding='utf-8')
def section(a,b):return s[s.index(a):s.index(b)]
c=r'''
#include <pthread.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "reimpl/pthr.h"
#define bionic_pthread_result(e) (e)
#define PTHR_INLINE static inline
#define WORKER_STATS_CAP 32
static atomic_int worker_stats_ids[WORKER_STATS_CAP];
static atomic_uintptr_t worker_stats_handles[WORKER_STATS_CAP];
typedef struct { unsigned size; int stackSize; void *stack; } SceKernelThreadInfo;
static int kernel_error;
static int sceKernelGetThreadId(void) { return 55; }
static int sceKernelGetThreadInfo(int tid,SceKernelThreadInfo *info) {
    assert(tid==55 || tid==99);
    if(kernel_error)return -1;
    info->stack=(void *)0x123000;info->stackSize=512*1024;return 0;
}
'''
c+=section('PTHR_INLINE int _attr_t_static_init','// null check for `mutex`')
c+=section('int pthread_attr_init_soloader(', 'int pthread_detach_soloader(')
c+=r'''
int main(void) {
    struct { pthread_attr_t_bionic a; unsigned canary; } value={0};value.canary=0xfeedface;
    pthread_attr_t_bionic *a=&value.a;
    assert(!pthread_attr_init_soloader(a));
    int detach;
    assert(pthread_attr_setdetachstate_soloader(a,2)==EINVAL);
    assert(!pthread_attr_setdetachstate_soloader(a,1));
    assert(!pthread_attr_getdetachstate(a->real_ptr,&detach) && detach==PTHREAD_CREATE_DETACHED);
    assert(!pthread_attr_setdetachstate_soloader(a,0));
    assert(!pthread_attr_getdetachstate(a->real_ptr,&detach) && detach==PTHREAD_CREATE_JOINABLE);
    struct { struct sched_param p; unsigned canary; } param={0};param.canary=0xfadedbad;
    assert(!pthread_attr_getschedparam_soloader(a,&param.p) && param.p.sched_priority==0);
    assert(!pthread_attr_setschedpolicy_soloader(a,0));
    assert(!pthread_attr_setschedparam_soloader(a,&param.p));
    assert(pthread_attr_setschedpolicy_soloader(a,2)==ENOTSUP);
    assert(pthread_attr_setschedpolicy_soloader(a,99)==EINVAL);
    param.p.sched_priority=1;assert(pthread_attr_setschedparam_soloader(a,&param.p)==EINVAL);
    param.p.sched_priority=0;
    assert(pthread_attr_setstacksize_soloader(a,8192)==EINVAL);
    assert(!pthread_attr_setstacksize_soloader(a,512*1024));
    void *stack;size_t size,native;
    assert(!pthread_attr_getstack_soloader(a,&stack,&size) && !stack && size==512*1024);
    assert(!pthread_attr_getstacksize(a->real_ptr,&native) && native==size);
    assert(pthread_attr_setstack_soloader(a,(void *)0x456000,512*1024)==ENOTSUP);
    assert(!pthread_getattr_np_soloader(pthread_self(),a));
    assert(!pthread_attr_getstack_soloader(a,&stack,&size) && stack==(void *)0x123000 && size==512*1024);
    kernel_error=1;assert(pthread_getattr_np_soloader(pthread_self(),a)==ESRCH);kernel_error=0;
    atomic_store(&worker_stats_handles[0],42);atomic_store(&worker_stats_ids[0],99);
    assert(!pthread_getattr_np_soloader((pthread_t)42,a));
    assert(pthread_getattr_np_soloader((pthread_t)43,a)==ESRCH);
    assert(pthread_getattr_np_soloader(0,a)==EINVAL);
    assert(value.canary==0xfeedface && param.canary==0xfadedbad);
    assert(!pthread_attr_destroy_soloader(a));
    assert(pthread_attr_getstack_soloader(a,&stack,&size)==EINVAL);
    assert(sched_get_priority_min_soloader(0)==0 && sched_get_priority_max_soloader(0)==0);
    assert(sched_get_priority_min_soloader(1)==1 && sched_get_priority_max_soloader(2)==99);
    assert(sched_get_priority_min_soloader(99)==-1 && errno==EINVAL);
    puts("PASS: initialized outputs/canaries, Android priorities, explicit stack sizes, real kernel stack geometry, current/known/missing threads, unsupported policies and caller-owned stacks");
}
'''
(w/'check.c').write_text(c);e=w/'check.exe'
subprocess.run(['gcc','-O2','-static','-pthread','-I'+str(src),str(w/'check.c'),'-o',str(e)],check=True,timeout=30)
subprocess.run([str(e)],check=True,timeout=5)
