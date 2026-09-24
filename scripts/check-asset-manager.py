"""Exercise production asset operations with real host files and fault injection."""
from pathlib import Path
import argparse, subprocess, tempfile
r=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=r/'vita/direct/source/reimpl/asset_manager.cpp');args=p.parse_args()
w=Path(tempfile.mkdtemp(prefix='asset-manager-',dir=r/'out'))
s=args.source.read_text()
# Replace platform-only declarations; the complete production implementation is retained.
for line in ['#include "reimpl/io.h"','#include "utils/logger.h"','#include <libc_bridge/libc_bridge.h>']:
    s=s.replace(line,'')
prefix=r'''
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cassert>
#include <climits>
#include <new>
#include <pthread.h>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <sys/types.h>
#include <malloc.h>
#include <unistd.h>
#define DATA_PATH ""
#define l_debug(...) ((void)0)
#define l_info(...) ((void)0)
#define l_warn(...) ((void)0)
#define l_error(...) ((void)0)
static unsigned opens,closes,reads,seeks,trims,reallocs;
static int fail_open,fail_alloc,fail_seek,short_read;
static FILE *test_fopen(const char*p,const char*m){++opens;if(fail_open){errno=fail_open;fail_open=0;return nullptr;}return fopen(p,m);}
static int test_fclose(FILE*f){++closes;return fclose(f);}
static size_t test_fread(void*b,size_t n,size_t c,FILE*f){++reads;if(short_read){short_read=0;c/=2;}return fread(b,n,c,f);}
static int test_fseek(FILE*f,long off,int whence){++seeks;if(fail_seek){fail_seek=0;return -1;}return fseek(f,off,whence);}
static void *test_malloc(size_t n){if(fail_alloc){fail_alloc=0;return nullptr;}return malloc(n);}
static char *test_strdup(const char*s){if(fail_alloc){fail_alloc=0;return nullptr;}return strdup(s);}
static void *test_realloc(void*p,size_t n){++reallocs;if(fail_alloc){fail_alloc=0;return nullptr;}return realloc(p,n);}
void asset_vfd_trim_cached_fds(unsigned){++trims;}
int asset_vfd_open(const char*p,off_t){return open(p,O_RDONLY);}
#define fopen test_fopen
#define fclose test_fclose
#define fread test_fread
#define fseek test_fseek
#define malloc test_malloc
#define strdup test_strdup
#define realloc test_realloc
'''
tests=r'''
#undef fopen
#undef fclose
#undef fread
#undef fseek
#undef malloc
#undef strdup
#undef realloc
int main(){
 auto *m=AAssetManager_create();assert(m && m==AAssetManager_fromJava(nullptr,nullptr));
 assert(!AAssetManager_open(m,nullptr,0));
 assert(!AAssetManager_open(m,std::string(2000,'x').c_str(),0));
 assert(!AAssetManager_open(m,"missing",0) && trims==0);
 auto *a=AAssetManager_open(m,"data",0);assert(a && AAsset_getLength(a)==256);
 char b[256];assert(AAsset_read(a,nullptr,0)==0 && AAsset_read(a,nullptr,1)==-1);
 assert(AAsset_read(a,b,17)==17 && AAsset_getRemainingLength(a)==239);
 assert(AAsset_seek(a,-18,SEEK_CUR)==-1 && AAsset_getRemainingLength(a)==239);
 assert(AAsset_seek(a,1,SEEK_END)==-1 && AAsset_seek(a,0,999)==-1);
 auto *buf=(const unsigned char *)AAsset_getBuffer(a);assert(buf && buf[255]==255);
 assert(AAsset_getRemainingLength(a)==239);
 unsigned old_reads=reads,old_seeks=seeks,old_opens=opens;
 for(int i=0;i<1000;++i){assert(AAsset_seek(a,17,SEEK_SET)==17);assert(AAsset_read(a,b,23)==23 && (unsigned char)b[0]==17);}
 assert(reads==old_reads && seeks==old_seeks && opens==old_opens);
 assert(AAsset_seek(a,0,SEEK_END)==256 && AAsset_read(a,b,1)==0);
 AAsset_close(a);
 /* Descriptor handoff conserves handles but preserves the independent cursor. */
 a=AAssetManager_open(m,"data",0);assert(a);assert(AAsset_read(a,b,12)==12);
 off_t start,length;int fd=AAsset_openFileDescriptor(a,&start,&length);
 assert(fd>=0 && start==0 && length==256);close(fd);
 assert(AAsset_getRemainingLength(a)==244 && AAsset_read(a,b,1)==1 && b[0]==12);
 /* Allocation, seek and truncated-read failures never consume logical bytes. */
 fail_alloc=1;assert(!AAsset_getBuffer(a) && AAsset_getRemainingLength(a)==243);
 short_read=1;assert(!AAsset_getBuffer(a) && AAsset_getRemainingLength(a)==243);
 assert(AAsset_read(a,b,1)==1 && b[0]==13);
 fail_seek=1;assert(AAsset_seek(a,0,SEEK_SET)==-1 && AAsset_getRemainingLength(a)==242);
 assert(AAsset_getBuffer(a) && AAsset_getRemainingLength(a)==242);AAsset_close(a);
 unsigned delta=closes;fail_alloc=1;assert(!AAssetManager_open(m,"data",0) && closes==delta+1);
 fail_seek=1;assert(!AAssetManager_open(m,"data",0));
 fail_open=EMFILE;assert((a=AAssetManager_open(m,"data",0)) && trims==1);AAsset_close(a);
 a=AAssetManager_open(m,"empty",0);assert(a && AAsset_read(a,b,1)==0);AAsset_close(a);
 unsigned before=reallocs;auto *d=AAssetManager_openDir(m,"many");assert(d);
 unsigned count=0;std::string prior;
 while(const char *n=AAssetDir_getNextFileName(d)){assert(prior.empty()||prior<n);prior=n;++count;}
 assert(count==1000 && reallocs-before<=7);AAssetDir_close(d);
 fail_alloc=1;assert(!AAssetManager_openDir(m,"many"));
 puts("PASS: independent cursor/buffer/FD lifetimes, null/OOM/seek/short-read handling, EOF and bounds");
 puts("PASS: 1000 buffered read/seek pairs make zero stdio calls; 1000 directory entries use 7 growth allocations; missing assets never trim FD cache");
}
'''
(w/'assets/many').mkdir(parents=True)
(w/'assets/data').write_bytes(bytes(range(256)));(w/'assets/empty').write_bytes(b'')
for i in range(1000):(w/'assets/many'/f'{i:04d}').write_bytes(b'')
(w/'check.cpp').write_text(prefix+s+tests)
subprocess.run(['g++','-std=c++14','-O2','-static','-I'+str(r/'vita/direct/source'),str(w/'check.cpp'),'-o',str(w/'check.exe')],check=True)
subprocess.run([str(w/'check.exe')],cwd=w,check=True,timeout=15)
print(w)
