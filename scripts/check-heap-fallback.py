"""Fault-injected game allocator ownership/budget tests, no device or game."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
work = Path(tempfile.mkdtemp(prefix='heap-fallback-', dir=root/'out'))
c = r'''
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
#include <pthread.h>
#include <unistd.h>
#include <malloc.h>
typedef int SceUID;
typedef struct {int size, size_user, size_cdram, size_phycont;} SceKernelFreeMemorySizeInfo;
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_RW 1
#define MIB (1024u*1024u)
static int heap_fail, kernel_fail, base_fail, info_fail;
static size_t user_total=32u*MIB, kernel_used, kernel_peak;
static unsigned kernel_calls, normal_live;
static atomic_uint heap_calls;
static struct {void *raw,*base;size_t bytes;} kernel[128];
static void telemetry_log(const char *tag,const char *fmt,...) {}
static int sceKernelDelayThread(unsigned us) {usleep(us);return 0;}
static int sceKernelGetFreeMemorySize(SceKernelFreeMemorySizeInfo *info) {
    info->size_user=(int)(user_total-kernel_used);return info_fail ? -1 : 0;
}
static int sceKernelAllocMemBlock(const char *name,int type,size_t bytes,void *opt) {
    ++kernel_calls;
    assert(type==SCE_KERNEL_MEMBLOCK_TYPE_USER_RW && !(bytes&4095));
    if(kernel_fail || bytes>user_total-kernel_used)return -1;
    int id;for(id=1;id<128 && kernel[id].raw;++id){}
    assert(id<128);
    void *raw=malloc(bytes+4095);assert(raw);
    void *base=(void*)(((uintptr_t)raw+4095)&~(uintptr_t)4095);
    kernel[id].raw=raw;kernel[id].base=base;kernel[id].bytes=bytes;
    kernel_used+=bytes;if(kernel_used>kernel_peak)kernel_peak=kernel_used;
    memset(base,0xa5,bytes);return id;
}
static int sceKernelGetMemBlockBase(int id,void **ptr) {
    assert(id>0 && id<128 && kernel[id].raw);
    if(base_fail)return -1;*ptr=kernel[id].base;return 0;
}
static int sceKernelFreeMemBlock(int id) {
    assert(id>0 && id<128 && kernel[id].raw);
    kernel_used-=kernel[id].bytes;free(kernel[id].raw);
    memset(&kernel[id],0,sizeof(kernel[id]));return 0;
}
typedef struct {void *raw;size_t size;} Header;
static void *native_align(size_t align,size_t size) {
    ++heap_calls;if(heap_fail)return NULL;
    if(!align || (align&(align-1)))return NULL;
    if(align<sizeof(Header))align=sizeof(Header);
    void *raw=malloc(size+align+sizeof(Header));assert(raw);
    uintptr_t at=((uintptr_t)raw+sizeof(Header)+align-1)&~(uintptr_t)(align-1);
    Header *h=(Header*)at-1;*h=(Header){raw,size};++normal_live;return (void*)at;
}
static void *native_malloc(size_t size) {return native_align(16,size);}
static void native_free(void *ptr) {
    if(!ptr)return;
    for(int i=1;i<128;++i)assert(ptr!=kernel[i].base);
    assert(normal_live);--normal_live;free(((Header*)ptr-1)->raw);
}
static size_t native_usable(void *ptr) {
    for(int i=1;i<128;++i)assert(ptr!=kernel[i].base);
    return ((Header*)ptr-1)->size;
}
static void *native_realloc(void *ptr,size_t size) {
    void *next=native_malloc(size);if(!next)return NULL;
    size_t old=native_usable(ptr);memcpy(next,ptr,old<size?old:size);native_free(ptr);return next;
}
static void *native_calloc(size_t n,size_t size) {
    void *ptr=native_malloc(n*size);if(ptr)memset(ptr,0,n*size);return ptr;
}
#define malloc native_malloc
#define calloc native_calloc
#define realloc native_realloc
#define memalign native_align
#define malloc_usable_size native_usable
#define free native_free
#define PVZ2_HEAP_HOST_TEST
#include "utils/heap_fallback.c"
#undef malloc
#undef calloc
#undef realloc
#undef free
static void empty(void) {assert(!kernel_used && !reserved_bytes && !normal_live);}
static void *stress(void *arg) {
    for(int i=0;i<150;++i) {
        unsigned char *p=pvz2_heap_malloc(256u*1024u);assert(p);
        memset(p,(int)(uintptr_t)arg,256u*1024u);
        unsigned char *q=pvz2_heap_realloc(p,512u*1024u);assert(q);
        for(unsigned j=0;j<256u*1024u;j+=4096)assert(q[j]==(unsigned)(uintptr_t)arg);
        pvz2_heap_free(q);
    }
    return NULL;
}
int main(void) {
    /* Ordinary success never consumes a kernel page, even for dump-sized calls. */
    void *p=pvz2_heap_malloc(0x480000);assert(p && !kernel_calls);
    pvz2_heap_free(p);empty();
    heap_fail=1;
    for(size_t size=0x480000;size<=0x600000;size+=0x180000) {
        p=pvz2_heap_malloc(size);assert(p && !((uintptr_t)p&4095));
        memset(p,0x67,size);pvz2_heap_free(p);empty();
    }
    puts("PASS: failed 4.5/6 MiB requests recover outside newlib; normal success uses no fallback");
    p=pvz2_heap_calloc(1024,1024);assert(p);
    for(unsigned i=0;i<MIB;++i)assert(((unsigned char*)p)[i]==0);
    pvz2_heap_free(p);
    unsigned calls=heap_calls;
    assert(!pvz2_heap_calloc(SIZE_MAX,2) && heap_calls==calls);
    p=pvz2_heap_memalign(4096,MIB);assert(p && !((uintptr_t)p&4095));pvz2_heap_free(p);
    assert(!pvz2_heap_memalign(8192,MIB));assert(!pvz2_heap_memalign(3,MIB));
    assert(!pvz2_heap_malloc(128));assert(!pvz2_heap_malloc(FALLBACK_MAX+1));
    kernel_fail=1;assert(!pvz2_heap_malloc(MIB));kernel_fail=0;
    base_fail=1;assert(!pvz2_heap_malloc(MIB));base_fail=0;empty();
    info_fail=1;assert(!pvz2_heap_malloc(MIB));info_fail=0;
    user_total=USER_HEADROOM+MIB-1;assert(!pvz2_heap_malloc(MIB));
    user_total=USER_HEADROOM+MIB;p=pvz2_heap_malloc(MIB);assert(p);pvz2_heap_free(p);
    user_total=32u*MIB;
    void *a=pvz2_heap_malloc(8u*MIB),*b=pvz2_heap_malloc(8u*MIB);
    assert(a && b && !pvz2_heap_malloc(MIB) && reserved_bytes==FALLBACK_BUDGET);
    pvz2_heap_free(a);pvz2_heap_free(b);empty();
    void *slots[FALLBACK_SLOTS];
    for(int i=0;i<FALLBACK_SLOTS;++i){slots[i]=pvz2_heap_malloc(FALLBACK_MIN);assert(slots[i]);}
    assert(!pvz2_heap_malloc(FALLBACK_MIN));
    for(int i=0;i<FALLBACK_SLOTS;++i)pvz2_heap_free(slots[i]);empty();
    puts("PASS: zeroing, alignment, overflow, page/base/info failures, 8 MiB headroom, 16 MiB budget and slot reuse");
    /* Normal -> fallback -> normal, including failure preservation. */
    heap_fail=0;p=pvz2_heap_malloc(MIB);assert(p);memset(p,0x52,MIB);
    heap_fail=1;void *q=pvz2_heap_realloc(p,2u*MIB);assert(q && !normal_live);
    for(unsigned i=0;i<MIB;++i)assert(((unsigned char*)q)[i]==0x52);
    assert(pvz2_heap_realloc(q,MIB)==q);
    kernel_fail=1;assert(!pvz2_heap_realloc(q,4u*MIB));kernel_fail=0;
    for(unsigned i=0;i<MIB;++i)assert(((unsigned char*)q)[i]==0x52);
    heap_fail=0;p=pvz2_heap_realloc(q,3u*MIB);assert(p && !kernel_used);
    for(unsigned i=0;i<MIB;++i)assert(((unsigned char*)p)[i]==0x52);
    assert(!pvz2_heap_realloc(p,0));empty();
    heap_fail=1;p=pvz2_heap_realloc(NULL,MIB);assert(p);
    assert(!pvz2_heap_realloc(p,0));empty();pvz2_heap_free(NULL);
    puts("PASS: realloc ownership transitions preserve data, shrink, NULL/zero and old pointer on failure");
    pthread_t threads[4];
    for(unsigned i=0;i<4;++i)assert(!pthread_create(&threads[i],NULL,stress,(void*)(uintptr_t)(i+1)));
    for(int i=0;i<4;++i)assert(!pthread_join(threads[i],NULL));empty();
    assert(kernel_peak<=FALLBACK_BUDGET);
    puts("PASS: 600 concurrent allocation/growth/free cycles return every page and slot");
}
'''
(work/'check.c').write_text(c)
exe = work/'check.exe'
subprocess.run(['gcc','-std=gnu11','-O2','-static','-pthread',
                '-I'+str(root/'vita/direct/source'),str(work/'check.c'),'-o',str(exe)],check=True)
run = subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
(work/'result.txt').write_text(run.stdout+run.stderr)
print(run.stdout+run.stderr,end='')
run.check_returncode()
print('Evidence:',work)
