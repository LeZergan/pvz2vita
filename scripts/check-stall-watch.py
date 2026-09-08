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
static char output[32000];static unsigned used,ticks,deleted,errors;
static int tid=1,create_error,start_error;static jmp_buf stopped;
static int sceKernelGetThreadId(void) { return tid; }
static int sceKernelGetThreadInfo(int id,SceKernelThreadInfo *i) {
    strcpy(i->name,"test");i->status=4;i->waitType=3;i->waitId=42;
    i->runClocks=123;i->currentCpuAffinityMask=0x60000;return 0;
}
static void bounded_log_write(int *fd,const char *path,const void *line,size_t n) {
    assert(!strcmp(path,"test/stall.log") && n<512 && used+n<sizeof(output));
    memcpy(output+used,line,n);used+=n;output[used]=0;*fd=1;
}
static int sceIoClose(int fd) { return 0; }
#define sceClibSnprintf snprintf
#define sceClibStrnlen strnlen
static int sceKernelCreateThread(const char *n,int(*f)(unsigned,void*),int p,int s,int a,int m,void*v) {
    assert(p==160 && s==16384 && m==0x60000);return create_error ? -1 : 2;
}
static int sceKernelStartThread(int t,unsigned n,void*v) { return start_error ? -1 : 0; }
static int sceKernelDeleteThread(int t) { ++deleted;return 0; }
static void telemetry_log(const char *a,const char *b,...) { ++errors; }
static void sceKernelDelayThread(unsigned us) {
    assert(us==1000000);++ticks;
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
    pvz2_stall_start();assert(!errors);
    for(tid=2;tid<100;++tid) {
        pvz2_stall_wait(PVZ2_WAIT_COND,0x1234,0x98123456);
        assert(own_slot>=0);pvz2_stall_wait_done();pvz2_stall_thread_exit();
        assert(own_slot==-1);
    }
    tid=2;pvz2_stall_wait(PVZ2_WAIT_COND,0x1234,0x98123456);
    if(!setjmp(stopped))watch_main(0,NULL);
    assert(count("[STALL]")==4 && count("[RESUMED]")==2);
    assert(count("unchanged=5s")==2 && count("unchanged=15s")==1 && count("unchanged=30s")==1);
    assert(strstr(output,"bridge=condition object=0x1234 caller=0x98123456"));
    assert(used<6000); /* No continuous logging through 30-second stall. */
    create_error=1;pvz2_stall_start();assert(errors==1 && deleted==0);
    create_error=0;start_error=1;pvz2_stall_start();assert(errors==2 && deleted==1);
    puts("PASS: 5/15/30-second snapshots only, recovery/repeated stall, wait caller, slot reuse, failed observer cleanup");
}
'''
(w/'check.c').write_text(c);e=w/'check.exe'
subprocess.run(['gcc','-O2','-static','-I'+str(src),str(w/'check.c'),'-o',str(e)],check=True,timeout=30)
subprocess.run([str(e)],check=True,timeout=3)
