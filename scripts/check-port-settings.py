"""Production preferences parser/transaction: malformed input and I/O faults."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
w=Path(tempfile.mkdtemp(prefix='port-settings-',dir=r/'out'))
code=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#ifdef _WIN32
#include <io.h>
#define real_sync _commit
#else
#define real_sync fsync
#endif
#define DATA_PATH "./"
static int fault;
static FILE *writing;
static FILE *test_open(const char *p,const char *m){FILE*f=fopen(p,m);if(strstr(p,".tmp")&&strchr(m,'w'))writing=f;return f;}
static int test_close(FILE*f){int fail=f==writing&&fault==5;if(f==writing)writing=NULL;int rc=fclose(f);return fail?-1:rc;}
static int test_sync(int fd){
 if(fault==1)return -1;
 int rc=real_sync(fd);
 if(fault==4){FILE*f=fopen("port_settings.txt.tmp","wb");assert(f);fputs("CORRUPTED",f);fclose(f);}
 return rc;
}
static int test_rename(const char*a,const char*b){
 if((fault==2&&strstr(a,".tmp"))||(fault==3&&!strcmp(a,"./port_settings.txt"))){errno=EIO;return -1;}
 return rename(a,b);
}
static int test_remove(const char*p){if(fault==6&&strstr(p,".old")){errno=EACCES;return -1;}return remove(p);}
#define fopen test_open
#define fclose test_close
#define fsync test_sync
#define rename test_rename
#define remove test_remove
#include "utils/port_locale.c"
#undef fopen
#undef fclose
#undef fsync
#undef rename
#undef remove
static void set(const char*s,size_t n){FILE*f=fopen("port_settings.txt","wb");assert(f);assert(fwrite(s,1,n,f)==n);assert(!fclose(f));}
static void defaults(void){pvz2_locale_load();assert(!pvz2_locale_override()&&pvz2_locale_index()==0&&!strcmp(pvz2_locale(),"en_US"));}
static void original(void){char b[16]={0};FILE*f=fopen("port_settings.txt","rb");assert(f);assert(fread(b,1,sizeof(b),f)==6&&!memcmp(b,"3 0 0\n",6));fclose(f);}
int main(void){
 const char*bad[]={"","3 1 6\n","3 2 0\n","3 -1 0\n","4 1 0\n","3 1 0 garbage","3 1 0 0","999999999999999999999999 1 0","3 1x 0","3 1","3 1 999999999999999999999999"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i){set(bad[i],strlen(bad[i]));defaults();}
 const char embedded[]="3 1 0\0ignored";set(embedded,sizeof(embedded));defaults();
 char huge[1000];memset(huge,' ',sizeof(huge));memcpy(huge,"3 1 0",5);set(huge,sizeof(huge));defaults();
 for(int i=0;i<6;++i){char b[16];snprintf(b,sizeof(b),"2 1 %d\n",i);set(b,strlen(b));defaults();}
 for(fault=1;fault<=6;++fault){
  set("3 0 0\n",6);defaults();
  if(fault==6){FILE*f=fopen("port_settings.txt.old","wb");assert(f);fputs("3 0 0\n",f);fclose(f);}
  assert(!pvz2_port_settings_save(1,2));original();defaults();
  remove("port_settings.txt.old");remove("port_settings.txt.tmp");
 }
 fault=0;
 for(int i=0;i<6;++i){assert(pvz2_port_settings_save(1,i));pvz2_locale_load();assert(pvz2_locale_override()&&pvz2_locale_index()==i);}
 assert(pvz2_port_settings_save(0,5));pvz2_locale_load();assert(!strcmp(pvz2_locale(),"en_US"));
 assert(!rename("port_settings.txt","port_settings.txt.old"));pvz2_locale_load();assert(!pvz2_locale_override()&&pvz2_locale_index()==5);
 assert(!pvz2_port_settings_save(2,0)&&!pvz2_port_settings_save(1,6));
 puts("PASS: bounded strict preferences parse; legacy Off/English migration; six persisted locales; default/off English; exact byte verification; sync/stage/commit/corruption/close/remove faults preserve old preferences; interrupted recovery");
}
'''
(w/'test.c').write_text(code);exe=w/'test.exe'
subprocess.run(['gcc','-O2','-static','-I',str(r/'vita/direct/source'),str(w/'test.c'),'-o',str(exe)],check=True,timeout=30)
run=subprocess.run([str(exe)],cwd=w,check=True,capture_output=True,text=True,timeout=10)
print(run.stdout.strip());print(w)
