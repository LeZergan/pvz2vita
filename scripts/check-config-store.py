"""Replay concurrent first use against the actual config implementation in java.c."""
from pathlib import Path
import argparse
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source', type=Path, default=root/'vita/direct/source/java.c')
a = p.parse_args()
source = a.source.read_text(encoding='utf-8')
code = source[source.index('#define CFG_MAX'):source.index('/* Offline gate flags')]
modern = 'static int cfg_get(' in code
work = Path(tempfile.mkdtemp(prefix='config-store-', dir=root/'out'))
prefix = r'''
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <assert.h>
#define DATA_PATH ""
#define l_info(...) ((void)0)
#define l_warn(...) ((void)0)
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static int entered, released, second_done, fail_write;
static FILE *gated_fopen(const char *path, const char *mode) {
    if(fail_write && strcmp(mode,"wb")==0) { errno=ENOSPC; return NULL; }
    if(strcmp(mode,"rb")==0 && !released) {
        pthread_mutex_lock(&gate);
        entered=1; pthread_cond_broadcast(&changed);
        while(!released) pthread_cond_wait(&changed,&gate);
        pthread_mutex_unlock(&gate);
    }
    return fopen(path,mode);
}
#define fopen gated_fopen
'''
adapter = r'''
static int read_seed(void) {
    char value[256];
    return cfg_get("seed",value,sizeof(value)) ? atoi(value) : -1;
}
''' if modern else r'''
static int read_seed(void) {
    cfg_load(); int at=cfg_find("seed");
    return at>=0 ? atoi(g_cfg_vals[at]) : -1;
}
'''
suffix = r'''
#undef fopen
static int first_value, second_value;
static void *first(void *arg) { first_value=read_seed(); return NULL; }
static void *second(void *arg) {
    second_value=read_seed();
    pthread_mutex_lock(&gate); second_done=1;
    pthread_cond_broadcast(&changed); pthread_mutex_unlock(&gate);
    return NULL;
}
int main(void) {
    FILE *f=fopen("config.kv","wb"); assert(f);
    fputs("seed\t42\n",f); assert(fclose(f)==0);
    pthread_t a,b; assert(!pthread_create(&a,NULL,first,NULL));
    pthread_mutex_lock(&gate);
    while(!entered) pthread_cond_wait(&changed,&gate);
    pthread_mutex_unlock(&gate);
    assert(!pthread_create(&b,NULL,second,NULL));
    pthread_mutex_lock(&gate);
    struct timespec until; clock_gettime(CLOCK_REALTIME,&until);
    until.tv_nsec+=200000000;
    if(until.tv_nsec>=1000000000) { until.tv_sec++; until.tv_nsec-=1000000000; }
    while(!second_done && pthread_cond_timedwait(&changed,&gate,&until)!=ETIMEDOUT) {}
    released=1; pthread_cond_broadcast(&changed); pthread_mutex_unlock(&gate);
    assert(!pthread_join(a,NULL)); assert(!pthread_join(b,NULL));
    if(first_value!=42 || second_value!=42) {
        fprintf(stderr,"FAIL: first-use reader observed partial store: first=%d second=%d\n",first_value,second_value);
        return 2;
    }
    puts("PASS: concurrent first-use reader sees the complete persisted store");
'''
if modern:
    suffix = suffix.replace('int main(void) {', r'''
static void *writer(void *arg) {
    int n=(int)(size_t)arg;
    char key[96], value[256], copy[256];
    memset(value,'A'+n,255); value[255]=0;
    for(int i=0;i<16;i++) {
        snprintf(key,sizeof(key),"writer-%d-%d",n,i);
        assert(cfg_set(key,value));
        assert(cfg_set("shared",value));
        assert(cfg_get("shared",copy,sizeof(copy)));
        for(int k=1;k<255;k++) assert(copy[k]==copy[0]);
        if(i&1) cfg_erase(key);
    }
    return NULL;
}
int main(void) {''')
    suffix += r'''
    assert(cfg_set("present-empty",""));
    char value[256];
    assert(cfg_get("present-empty",value,sizeof(value)) && value[0]==0);
    assert(!cfg_get("missing",value,sizeof(value)));
    fail_write=1; assert(!cfg_set("save-failure","test")); fail_write=0;
    assert(cfg_set("after-failure","ok"));
    assert(cfg_get("after-failure",value,sizeof(value)) && !strcmp(value,"ok"));
    cfg_erase("seed"); assert(!cfg_get("seed",value,sizeof(value)));
    puts("PASS: missing/empty distinction, write failure reporting, recovery and erase");
    pthread_t writers[4];
    for(size_t i=0;i<4;i++) assert(!pthread_create(&writers[i],NULL,writer,(void *)i));
    for(int i=0;i<4;i++) assert(!pthread_join(writers[i],NULL));
    /* Reload committed bytes, independently of the in-memory table. */
    g_cfg_loaded=0; g_cfg_count=0;
    for(int n=0;n<4;n++) for(int i=0;i<16;i++) {
        char key[96]; snprintf(key,sizeof(key),"writer-%d-%d",n,i);
        int found=cfg_get(key,value,sizeof(value));
        assert(found==!(i&1));
        if(found) for(int k=0;k<255;k++) assert(value[k]=='A'+n);
    }
    puts("PASS: concurrent writers/readers/erase preserve complete values and survive reload");
'''
suffix += 'return 0;\n}\n'
(work/'check.c').write_text(prefix+code+adapter+suffix, encoding='utf-8')
exe = work/'check.exe'
subprocess.run(['gcc','-std=gnu11','-O2','-static','-pthread',str(work/'check.c'),'-o',str(exe)],check=True,timeout=30)
run = subprocess.run([str(exe)],cwd=work,capture_output=True,text=True,timeout=10)
(work/'result.txt').write_text(run.stdout+run.stderr,encoding='utf-8')
print(run.stdout+run.stderr,end='')
print(work)
raise SystemExit(run.returncode)
