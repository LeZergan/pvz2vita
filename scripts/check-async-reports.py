"""Production telemetry SPSC stress, stalled storage, retry and index wrap."""
from pathlib import Path
import subprocess, tempfile
r=Path(__file__).resolve().parents[1]
w=Path(tempfile.mkdtemp(prefix='async-reports-',dir=r/'out'))
s=(r/'vita/direct/source/utils/telemetry.c').read_text()
s='\n'.join(l for l in s.splitlines() if not l.startswith('#include "utils/') and not l.startswith('#include <psp2/'))
c=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <pthread.h>
#include <string.h>
#include <sched.h>
#define DATA_PATH "test/"
#define SCE_O_WRONLY 1
#define SCE_O_CREAT 2
#define SCE_O_TRUNC 4
#define SCE_O_APPEND 8
#define sceClibVsnprintf vsnprintf
#define sceClibSnprintf snprintf
#define sceClibStrnlen strnlen
#define sceClibPrintf(...) ((void)0)
typedef int SceUID;
static unsigned opens, writes, last, fail_io;
static int sceIoOpen(const char *p,int f,int m) { ++opens;return fail_io ? -1:1; }
static int sceIoClose(int fd) { return 0; }
static void bounded_log_write(int *fd,const char *p,const char *line,size_t n) {
    unsigned seq,payload;assert(n<1700 && line[n-1]=='\n');
    assert(sscanf(line,"[FPS] n=%u payload=%u",&seq,&payload)==2);
    assert(seq>last && payload==seq*17 && strstr(line,"[REPORTQ] dropped="));last=seq;++writes;
}
int telemetry_try_line(const char *line);
'''+s+r'''
static atomic_int done;
static void *consumer(void *unused) {
    while(!atomic_load(&done)) { telemetry_reports_drain();sched_yield(); }
    telemetry_reports_drain();return NULL;
}
int main(void) {
    telemetry_report("FPS","n=1 payload=17");assert(!opens && !atomic_load(&g_report_write));
    telemetry_reports_enable();
    pthread_mutex_lock(&g_trace_mutex);
    for(unsigned i=1;i<=10000;++i)telemetry_report("FPS","n=%u payload=%u",i,i*17);
    assert(!opens && g_reports_dropped==9998);
    telemetry_reports_drain();assert(!opens);
    pthread_mutex_unlock(&g_trace_mutex);
    fail_io=1;telemetry_reports_drain();assert(!writes && !atomic_load(&g_report_read));
    fail_io=0;telemetry_reports_drain();assert(writes==2 && last==2);
    atomic_store(&g_report_write,UINT32_MAX-1);atomic_store(&g_report_read,UINT32_MAX-1);
    telemetry_report("FPS","n=3 payload=51");telemetry_report("FPS","n=4 payload=68");
    telemetry_reports_drain();assert(writes==4 && !atomic_load(&g_report_read));
    pthread_t t;assert(!pthread_create(&t,NULL,consumer,NULL));
    for(unsigned i=5;i<500005;++i)telemetry_report("FPS","n=%u payload=%u",i,i*17);
    atomic_store(&done,1);pthread_join(t,NULL);
    assert(atomic_load(&g_report_read)==atomic_load(&g_report_write));
    assert(writes>4 && writes+g_reports_dropped==510002);
    puts("PASS: 500,000 concurrent reports preserve complete ordered payloads; full queue and locked/failed storage never block producer; retry, disabled observer and 32-bit wrap");
}
'''
(w/'check.c').write_text(c)
subprocess.run(['gcc','-O2','-static','-pthread',str(w/'check.c'),'-o',str(w/'check.exe')],check=True)
run=subprocess.run([str(w/'check.exe')],check=True,capture_output=True,text=True,timeout=30)
(w/'result.txt').write_text(run.stdout)
print(run.stdout.strip());print(w)
