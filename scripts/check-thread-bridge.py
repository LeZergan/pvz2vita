"""Small noninteractive correctness check of production Bionic mutex/cond code.

Native host pthreads substitute for Vita pthreads; this is not a FPS benchmark.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = root / 'vita/direct/source'
work = Path(tempfile.mkdtemp(prefix='thread-bridge-', dir=root / 'out'))
source = (src / 'reimpl/pthr.c').read_text(encoding='utf-8')
def section(start, end):
    return source[source.index(start):source.index(end)]

code = '''
#include <stdint.h>
#include <pthread.h>
#include <stdatomic.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include "reimpl/pthr.h"
#define sceClibMemset memset
/* POSIX permits undefined behavior destroying a locked normal mutex; inject
 * Vita/native destroy errors to check that the bridge preserves its object. */
static int destroy_error;
static int checked_destroy(pthread_mutex_t *p) {
    if(destroy_error) return destroy_error;
    return pthread_mutex_destroy(p);
}
#define pthread_mutex_destroy checked_destroy
'''
code += section('#define PTHR_MAX_OBJECTS', '/* Keep game/render')
code += section('int pthread_mutex_init_soloader(', 'int pthread_join_soloader(')
code += section('int pthread_cond_init_soloader(', 'int pthread_attr_init_soloader(')
code += r'''
static pthread_mutex_t_bionic shared;
static pthread_cond_t_bionic changed;
static unsigned counter, ready;
static void *increment(void *arg) {
    for(unsigned i=0;i<1000;++i) {
        assert(!pthread_mutex_lock_soloader(&shared));
        ++counter;
        assert(!pthread_mutex_unlock_soloader(&shared));
    }
    return NULL;
}
static void *signal_ready(void *arg) {
    assert(!pthread_mutex_lock_soloader(&shared));
    ready=1;
    assert(!pthread_cond_signal_soloader(&changed));
    assert(!pthread_mutex_unlock_soloader(&shared));
    return NULL;
}
int main(void) {
    /* Full table, collisions, backshift deletion, pointer reuse, >1024 objects. */
    pthread_mutex_t_bionic objects[PTHR_MAX_OBJECTS+1]={0};
    for(unsigned i=0;i<PTHR_MAX_OBJECTS;++i)
        assert(!pthread_mutex_init_soloader(&objects[i],NULL));
    assert(pthread_mutex_lock_soloader(&objects[PTHR_MAX_OBJECTS])==EAGAIN);
    assert(!objects[PTHR_MAX_OBJECTS].real_ptr);
    for(unsigned i=0;i<PTHR_MAX_OBJECTS;i+=3)
        assert(!pthread_mutex_destroy_soloader(&objects[i]));
    for(unsigned i=0;i<PTHR_MAX_OBJECTS;++i) {
        assert(isObjectInitialized(&objects[i])==(i%3!=0));
        assert(!pthread_mutex_lock_soloader(&objects[i]));
        assert(!pthread_mutex_unlock_soloader(&objects[i]));
    }
    for(unsigned i=PTHR_MAX_OBJECTS;i--;) assert(!pthread_mutex_destroy_soloader(&objects[i]));
    for(unsigned i=0;i<PTHR_MAX_OBJECTS;++i) assert(!atomic_load(&initializedObjects[i]));
    pthread_t workers[4];
    for(unsigned i=0;i<4;++i) assert(!pthread_create(&workers[i],NULL,increment,NULL));
    for(unsigned i=0;i<4;++i) assert(!pthread_join(workers[i],NULL));
    assert(counter==4000);
    assert(!pthread_mutex_lock_soloader(&shared));
    pthread_mutex_t *original=shared.real_ptr;
    destroy_error=EBUSY;
    assert(pthread_mutex_destroy_soloader(&shared)==EBUSY);
    destroy_error=0;
    assert(shared.real_ptr==original && isObjectInitialized(&shared));
    assert(!pthread_create(&workers[0],NULL,signal_ready,NULL));
    while(!ready) assert(!pthread_cond_wait_soloader(&changed,&shared));
    assert(!pthread_mutex_unlock_soloader(&shared));
    assert(!pthread_join(workers[0],NULL));
    assert(!pthread_cond_destroy_soloader(&changed));
    assert(!pthread_mutex_destroy_soloader(&shared));
    pthread_mutex_t_bionic recursive={(void *)0x4000};
    assert(!pthread_mutex_lock_soloader(&recursive));
    assert(!pthread_mutex_trylock_soloader(&recursive));
    assert(!pthread_mutex_unlock_soloader(&recursive));
    assert(!pthread_mutex_unlock_soloader(&recursive));
    assert(!pthread_mutex_destroy_soloader(&recursive));
    puts("PASS: 8192 mutexes/full-table failure; collisions/deletion/reuse; concurrent first-use and mutual exclusion; condition wakeup; busy destroy; recursive static initializer");
}
'''
(work / 'check.c').write_text(code, encoding='utf-8')
exe = work / 'check.exe'
subprocess.run(['gcc', '-std=gnu11', '-O2', '-static', '-pthread', '-I'+str(src),
                str(work/'check.c'), '-o', str(exe)], check=True, timeout=30)
run = subprocess.run([str(exe)], check=True, capture_output=True, text=True, timeout=8)
(work / 'result.txt').write_text(run.stdout, encoding='utf-8')
print(run.stdout.strip())
print(work)
