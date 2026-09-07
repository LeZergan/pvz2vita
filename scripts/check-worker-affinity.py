"""Small production worker-launch/affinity check; mocked Vita calls, no UI."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
src=root/'vita/direct/source'
work=Path(tempfile.mkdtemp(prefix='worker-affinity-',dir=root/'out'))
p=(src/'reimpl/pthr.c').read_text(encoding='utf-8')
def part(a,b): return p[p.index(a):p.index(b)]
c=r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <stdatomic.h>
#include "reimpl/pthr.h"
typedef int SceUID;
static atomic_int g_core3_mask=0x60000;
#define WORKER_STATS_CAP 32
static atomic_int worker_stats_ids[WORKER_STATS_CAP];
static _Thread_local int actual_mask=0x10000;
static int allowed_mask=0x60000, lying, fail_alloc, create_calls;
static atomic_int executed, observed;
static int sceKernelGetThreadId(void) { return 1; }
static int sceKernelGetThreadCpuAffinityMask(int t) { return actual_mask; }
static int sceKernelChangeThreadCpuAffinityMask(int t,int m) {
    if (m & ~allowed_mask) return -1;
    if (!lying) actual_mask=m;
    return 1; /* A nonnegative value is success, not only zero. */
}
static void sceKernelDelayThread(int us) {}
static void telemetry_log(const char *tag,const char *fmt,...) {}
#define l_warn(...) ((void)0)
static void *checked_malloc(size_t n) { if(fail_alloc) return NULL; return malloc(n); }
static int counted_create(pthread_t *t,const pthread_attr_t *a,void *(*f)(void *),void *v) {
    ++create_calls; return pthread_create(t,a,f,v);
}
#define malloc checked_malloc
#define pthread_create counted_create
#define PTHR_INLINE static inline
'''
c+=part('PTHR_INLINE int _attr_t_static_init','// null check for `mutex`')
c+=part('void pvz2_init_thread_affinity(void)', 'int pthread_mutexattr_init_soloader(')
c+=r'''
static void *job(void *arg) {
    atomic_store(&observed,actual_mask);
    atomic_fetch_add(&executed,1);
    free(arg); /* A successful launch must not read this pointer again. */
    return (void *)42;
}
int main(void) {
    allowed_mask=0x70000; pvz2_init_thread_affinity();
    assert(pvz2_cpu_core_count()==3 && actual_mask==0x10000);
    allowed_mask=0xf0000; pvz2_init_thread_affinity();
    assert(pvz2_cpu_core_count()==4 && actual_mask==0x10000);
    lying=1; pvz2_init_thread_affinity();
    assert(pvz2_cpu_core_count()==3); lying=0;
    allowed_mask=0x60000;
    assert(pvz2_cpu_core_count()==3);
    atomic_store(&g_core3_mask,0xe0000); assert(pvz2_cpu_core_count()==4);
    assert(!worker_apply_affinity(1) && actual_mask==0x60000); /* Locked core 3. */
    allowed_mask=0x20000; actual_mask=0x10000;
    assert(!worker_apply_affinity(1) && actual_mask==0x20000);
    allowed_mask=0x40000; actual_mask=0x10000;
    assert(!worker_apply_affinity(1) && actual_mask==0x40000);
    allowed_mask=0xe0000; assert(!worker_apply_affinity(1) && actual_mask==0xe0000);
    lying=1; actual_mask=0x10000; assert(worker_apply_affinity(1)<0); lying=0;
    allowed_mask=0; assert(worker_apply_affinity(1)<0);
    allowed_mask=0x60000; atomic_store(&g_core3_mask,0x60000);
    pthread_t threads[2]; void *result;
    for(int i=0;i<2;++i) assert(!pthread_create_soloader(&threads[i],NULL,job,malloc(8)));
    for(int i=0;i<2;++i) { assert(!pthread_join(threads[i],&result)); assert(result==(void *)42); }
    assert(atomic_load(&executed)==2 && atomic_load(&observed)==0x60000);
    int before=create_calls; fail_alloc=1;
    assert(pthread_create_soloader(&threads[0],NULL,job,NULL)==ENOMEM);
    assert(create_calls==before && atomic_load(&executed)==2);
    pthread_attr_t_bionic attr={0};
    assert(_attr_t_static_init(&attr)==ENOMEM && !attr.magic && !attr.real_ptr);
    fail_alloc=0;
    puts("PASS: normal/unlocked core counts; verified mask and core-3/single-core fallbacks; failed affinity detection; actual host worker launch through affinity wrapper; OOM never bypasses wrapper");
}
'''
(work/'check.c').write_text(c,encoding='utf-8')
exe=work/'check.exe'
subprocess.run(['gcc','-std=gnu11','-O2','-static','-pthread','-I'+str(src),str(work/'check.c'),'-o',str(exe)],check=True,timeout=30)
run=subprocess.run([str(exe)],check=True,capture_output=True,text=True,timeout=5)
(work/'result.txt').write_text(run.stdout,encoding='utf-8')
print(run.stdout.strip()); print(work)
