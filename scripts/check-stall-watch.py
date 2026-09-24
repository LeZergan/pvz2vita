"""Observer regression: freezes, recovery, thread reuse, failed startup; no UI."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1];src=r/'vita/direct/source'
w=Path(tempfile.mkdtemp(prefix='stall-watch-',dir=r/'out'))
s=(src/'utils/stall_watch.c').read_text()
s='\n'.join(line for line in s.splitlines() if not line.startswith('#include'))
c=r'''
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdatomic.h>
#include <setjmp.h>
#include "utils/stall_watch.h"
#define DATA_PATH "test/"
typedef int SceUID;
typedef struct { unsigned size; char name[32]; unsigned status,waitType,waitId;
    unsigned long long runClocks; unsigned currentCpuAffinityMask; } SceKernelThreadInfo;
static int pvz2_logging_enabled=1;
static char output[32000];static unsigned used,ticks,deleted,errors,starts;
static int tid=1,create_error,start_error,moving;static unsigned tid_queries;static jmp_buf stopped;
static int sceKernelGetThreadId(void) { ++tid_queries; return tid; }
static int sceKernelGetThreadInfo(int id,SceKernelThreadInfo *i) {
    if(id>=100 && id!=tid)return -1;
    strcpy(i->name,"test");i->status=4;i->waitType=3;i->waitId=42;
    i->runClocks=123;i->currentCpuAffinityMask=0x60000;return 0;
}
static void bounded_log_write(int *fd,const char *path,const void *line,size_t n) {
    assert(!strcmp(path,"test/stall.log") && n<512 && used+n<sizeof(output));
    memcpy(output+used,line,n);used+=n;output[used]=0;*fd=1;
}
static int sceIoClose(int fd) { return 0; }
static unsigned mirrored;
static void telemetry_reports_enable(void) {}
static void telemetry_reports_drain(void) {}
static int telemetry_try_line(const char *line) { ++mirrored; return 1; }
#define sceClibSnprintf snprintf
#define sceClibStrnlen strnlen
static int sceKernelCreateThread(const char *n,int(*f)(unsigned,void*),int p,int s,int a,int m,void*v) {
    assert(p==160 && s==16384 && m==0x70000);return create_error ? -1 : 2;
}
static int sceKernelStartThread(int t,unsigned n,void*v) { return start_error ? -1 : 0; }
static int sceKernelDeleteThread(int t) { ++deleted;return 0; }
static void telemetry_log(const char *a,const char *b,...) {
    if(!strcmp(a,"SYMBOLS")) { assert(strstr(b,"stall_start")); return; }
    if(strstr(b,"unavailable")) ++errors;
    else { assert(strstr(b,"test/stall.log")); ++starts; }
}
static void sceKernelDelayThread(unsigned us) {
    assert(us==1000000);++ticks;
    if(moving) {
        pvz2_stall_frame(ticks,PVZ2_FRAME_DRAW);
        if(ticks==43)longjmp(stopped,1);
        return;
    }
    if(ticks<5) pvz2_stall_frame(ticks,PVZ2_FRAME_DRAW);
    if(ticks==36) pvz2_stall_frame(5,PVZ2_FRAME_PRESENT);
    if(ticks==42) pvz2_stall_frame(6,PVZ2_FRAME_DRAW);
    if(ticks==43) longjmp(stopped,1);
}
'''+s+r'''
static unsigned count(const char *text) {
    unsigned n=0;const char *p=output;while((p=strstr(p,text))){++n;p+=strlen(text);}return n;
}
int main(void) {
    pvz2_logging_enabled=0;
    pvz2_stall_start();pvz2_stall_frame(1,1);pvz2_stall_wait(1,1,1);
    assert(pvz2_stall_native_wait(1,1,1)==-1 && pvz2_stall_sync(1,1,1,1)==-1);
    pvz2_stall_wait_done();pvz2_stall_thread_exit();
    assert(!tid_queries && !starts && !used);
    pvz2_logging_enabled=1;
    pvz2_stall_start();assert(!errors && starts==1);
    for(tid=2;tid<100;++tid) {
        pvz2_stall_wait(PVZ2_WAIT_COND,0x1234,0x98123456);
        assert(own_slot>=0);pvz2_stall_wait_done();pvz2_stall_thread_exit();
        assert(own_slot==-1);
    }
    tid=2;pvz2_stall_wait(PVZ2_WAIT_COND,0x1234,0x98123456);
    int sync_token=pvz2_stall_sync(PVZ2_SYNC_WAIT,0x9870,0x9874,0x81067890);
    int token=pvz2_stall_native_wait(PVZ2_NATIVE_SEMA,0x5678,0x81012345);
    assert(sync_token==token);
    assert(atomic_load(&slots[own_slot].kind)==PVZ2_WAIT_COND);
    if(!setjmp(stopped))watch_main(0,NULL);
    assert(count("[STALL]")==4 && count("[RESUMED]")==2);
    assert(count("unchanged=5s")==2 && count("unchanged=15s")==1 && count("unchanged=30s")==1);
    assert(strstr(output,"bridge=condition object=0x1234 caller=0x98123456"));
    assert(strstr(output,"native=semaphore object=0x5678 caller=0x81012345"));
    assert(strstr(output,"operation=condition-wait condition=0x9870 mutex=0x9874 caller=0x81067890"));
    assert(mirrored>4);
    assert(token==own_slot);
    tid_queries=0;
    pvz2_stall_native_done(token);assert(!atomic_load(&slots[own_slot].native_kind));
    assert(!tid_queries); /* completion must not query the kernel again */
    assert(atomic_load(&sync_slots[sync_token].kind)==PVZ2_SYNC_WAIT);
    pvz2_stall_sync_done(sync_token);
    assert(!tid_queries && !atomic_load(&sync_slots[sync_token].kind));
    pvz2_stall_sync(PVZ2_SYNC_SIGNAL,1,0,2);
    pvz2_stall_thread_exit();
    assert(!atomic_load(&sync_slots[sync_token].kind));
    /* Native-only threads (audio/driver) need no TLS and can recycle exited
     * thread slots even if they never called the Android exit wrapper. */
    for(tid=100;tid<220;++tid) {
        token=pvz2_stall_native_wait(PVZ2_NATIVE_READ,7,0x81022345);
        assert(own_slot==-1 && native_slot(0)>=0);
        assert(atomic_load(&slots[native_slot(0)].native_kind)==PVZ2_NATIVE_READ);
        pvz2_stall_native_done(token);
    }
    assert(used<6000); /* No continuous logging through 30-second stall. */
    used=ticks=0;output[0]=0;moving=1;
    if(!setjmp(stopped))watch_main(0,NULL);
    assert(count("[SAMPLE]")==1 && count("[STALL]")==0);
    for(unsigned i=0;i<WATCH_SLOTS;++i) {
        atomic_store(&slots[i].tid,i+1); /* all live, no slot to reclaim */
        atomic_store(&slots[i].native_kind,PVZ2_NATIVE_MUTEX);
    }
    tid=400;token=pvz2_stall_native_wait(PVZ2_NATIVE_READ,7,0x81022345);
    assert(token==-1);
    assert(pvz2_stall_sync(PVZ2_SYNC_BROADCAST,1,0,2)==-1);
    pvz2_stall_sync_done(-1);pvz2_stall_sync_done(WATCH_SLOTS);
    tid_queries=0;pvz2_stall_native_done(token);pvz2_stall_native_done(WATCH_SLOTS);
    assert(!tid_queries);
    for(unsigned i=0;i<WATCH_SLOTS;++i)assert(atomic_load(&slots[i].native_kind)==PVZ2_NATIVE_MUTEX);
    create_error=1;pvz2_stall_start();assert(errors==1 && deleted==0);
    create_error=0;start_error=1;pvz2_stall_start();assert(errors==2 && deleted==1);
    puts("PASS: 5/15/30-second snapshots only, recovery/repeated stall, wait caller, slot reuse, failed observer cleanup");
}
'''
(w/'check.c').write_text(c);e=w/'check.exe'
subprocess.run(['gcc','-O2','-static','-I'+str(src),str(w/'check.c'),'-o',str(e)],check=True,timeout=30)
subprocess.run([str(e)],check=True,timeout=3)
