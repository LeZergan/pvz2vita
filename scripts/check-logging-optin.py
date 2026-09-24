"""Production runtime/JNI loggers: disabled hot path, enabled output and failures."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1];w=Path(tempfile.mkdtemp(prefix='logging-optin-',dir=r/'out'))
def source(path):
 return '\n'.join(l for l in (r/path).read_text(encoding='utf-8').splitlines() if not l.startswith('#include'))
c=r'''
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>
#include <string.h>
#define DATA_PATH "test/"
#define SCE_O_RDONLY 1
#define SCE_O_WRONLY 2
#define SCE_O_CREAT 4
#define SCE_O_APPEND 8
#define SCE_O_TRUNC 16
#define LT_DEBUG 0
#define LT_INFO 1
#define LT_WARN 2
#define LT_ERROR 3
#define LT_FATAL 4
#define LT_SUCCESS 5
#define LT_WAIT 6
#define FALSOJNI_DEBUGLEVEL 0
#define FALSOJNI_DEBUG_ALL 0
#define FALSOJNI_DEBUG_INFO 1
#define FALSOJNI_DEBUG_WARN 2
#define FALSOJNI_DEBUG_ERROR 3
#define sceClibSnprintf snprintf
#define sceClibVsnprintf vsnprintf
#define sceClibStrnlen strnlen
#define sceClibMemcpy memcpy
typedef int SceUID;
typedef int SceKernelLwMutexWork;
static int pvz2_logging_enabled,fail_create;
static unsigned opens,writes,creates,locks,console,closes;
static int sceIoOpen(const char*p,int flags,int mode){++opens;assert(!strncmp(p,"test/",5));return flags==SCE_O_RDONLY?-1:1;}
static int sceIoClose(int fd){++closes;return 0;}
static int sceKernelCreateLwMutex(int*p,const char*n,int a,int b,void*v){++creates;return fail_create?-1:0;}
static int sceKernelLockLwMutex(int*p,int n,void*v){++locks;return 0;}
static int sceKernelUnlockLwMutex(int*p,int n){return 0;}
static int sceClibPrintf(const char*f,...){++console;return 0;}
static void bounded_log_write(int*f,const char*p,const char*l,size_t n){assert(n>0 && n<32768);++writes;}
'''+source('vita/direct/source/utils/logger.c')+'\n'+source('vita/direct/lib/falso_jni/FalsoJNI_Logger.c')+r'''
int main(int argc,char**argv){
 for(unsigned i=0;i<1000000;i++){
  log_reset_file();_log_print(LT_ERROR,"silent %u",i);
  _fjni_log_error("file",1,"function","silent %u",i);
 }
 assert(!opens && !writes && !creates && !locks && !console && !closes);
 pvz2_logging_enabled=1;fail_create=argc>1;
 _log_print(LT_ERROR,"runtime enabled");_fjni_log_error("file",1,"function","jni enabled");
 if(fail_create){assert(creates==2 && !opens && !locks && !writes);}
 else {assert(creates==2 && locks==2 && writes==2 && console==1);log_reset_file();assert(closes==2);}
 puts("PASS: 1,000,000 disabled runtime/JNI/reset calls do no file, mutex or console work; enabled output and mutex-failure paths checked");
}
'''
(w/'check.c').write_text(c,encoding='utf-8')
subprocess.run(['gcc','-std=gnu11','-O2','-static','-pthread',str(w/'check.c'),'-o',str(w/'check.exe')],check=True)
for args in [[],['fail']]:subprocess.run([str(w/'check.exe'),*args],check=True,timeout=5)
print(w)
