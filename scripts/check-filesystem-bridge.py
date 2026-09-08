"""Production ABI/EOF/short-I/O regression checks; no device or user files."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1];src=root/'vita/direct/source'
w=Path(tempfile.mkdtemp(prefix='filesystem-bridge-',dir=root/'out'))
io=(src/'reimpl/io.c').read_text();hdr=(src/'reimpl/io.h').read_text()
sys=(src/'reimpl/sys.c').read_text();utils=(src/'utils/utils.c').read_text()
conv=(src/'reimpl/bits/_struct_converters.c').read_text()
code=r'''
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#define l_info(...) ((void)0)
#define l_warn(...) ((void)0)
#define l_debug(...) ((void)0)
#define l_error(...) ((void)0)
typedef struct { int64_t max_size,free_size; uint32_t cluster_size; void *unk; } SceIoDevInfo;
static SceIoDevInfo volume={256LL<<30,128LL<<30,32768,0};
static int device_error;
static const char *remap_android_path(const char *p) { return p; }
static int sceIoDevctl(const char *dev,unsigned cmd,void *in,int inlen,void *out,int outlen) {
    assert(!strcmp(dev,"ux0:"));assert(cmd==0x3001 && outlen==sizeof(volume));
    if(device_error)return -1;
    memcpy(out,&volume,sizeof(volume));return 0;
}
'''
code+=sys[sys.index('typedef struct bionic_statfs_compat'):sys.index('ssize_t writev_soloader')]
code+=hdr[hdr.index('typedef struct dirent64_bionic'):hdr.index('int open_soloader')]
code+=r'''
struct dirent { struct { int st_mode; } d_stat; char d_name[256]; };
typedef struct { int count,error; struct dirent entry; } DIR;
#define SCE_S_ISDIR(m) ((m)==4)
#define DT_DIR 4
#define DT_REG 8
static struct dirent *readdir(DIR *d) {
    if(d->error){errno=d->error;return NULL;}
    if(d->count++==0)return &d->entry;
    return NULL;
}
static int readdir_r(DIR *d,struct dirent *entry,struct dirent **result) {
    struct dirent *found=readdir(d);*result=NULL;
    if(d->error)return d->error;
    if(found){*entry=*found;*result=entry;}return 0;
}
'''
code+=conv[conv.index('void dirent_newlib_to_bionic'):conv.index('/**\n * Convert newlib (Vita) `stat`')]
code+=io[io.index('struct dirent64_bionic * readdir_soloader'):io.index('int closedir_soloader')]
code+=r'''
static int source_missing,rename_calls;
static int test_stat(const char *s,struct stat *out) {
    if(source_missing){errno=ENOENT;return -1;}memset(out,0,sizeof(*out));return 0;
}
static int test_rename(const char *a,const char *b) { ++rename_calls;return 0; }
static void ensure_parent_dirs_for_path(const char *p,int mode) {}
static int path_is_savedata_bundle(const char *p) {return 0;}
#define stat(path,buf) test_stat(path,buf)
#define rename test_rename
'''
code+=io[io.index('int rename_soloader'):io.index('int unlink_soloader')]
code+=r'''
#undef stat
#undef rename
static int io_error,opens,closes,allocations;
static void *test_malloc(size_t size) {
    if(io_error==5)return NULL;
    void *p=malloc(size);if(p)++allocations;return p;
}
static void test_free(void *p) {if(p)--allocations;free(p);}
static FILE *test_fopen(const char *path,const char *mode) {
    ++opens;return io_error==1 ? NULL : (FILE*)(uintptr_t)1;
}
static int test_fseek(FILE *f,long offset,int whence) { return io_error==2 ? -1 : 0; }
static long test_ftell(FILE *f) { return io_error==3 ? -1 : io_error==4 ? 0 : 16; }
static size_t test_fread(void *p,size_t size,size_t n,FILE *f) {
    size_t got=io_error==6 ? n/2 : n;memset(p,0x42,got);return got;
}
static size_t test_fwrite(const void *p,size_t size,size_t n,FILE *f) { return io_error==6 ? n/2 : n; }
static int test_fclose(FILE *f) { ++closes;return io_error==7 ? -1 : 0; }
static bool file_mkpath(const char *path,int mode) {return true;}
static bool file_save_sceio(const char *p,const uint8_t *b,size_t s) {return false;}
#define malloc test_malloc
#define free test_free
#define fopen test_fopen
#define fseek test_fseek
#define ftell test_ftell
#define fread test_fread
#define fwrite test_fwrite
#define fclose test_fclose
'''
code+=utils[utils.index('bool file_load('):utils.index('bool file_mkpath(')]
code+=utils[utils.index('bool file_save('):utils.index('size_t file_size(')]
code+=r'''
static pthread_barrier_t barrier;
static void *scan_thread(void *arg) {
    intptr_t id=(intptr_t)arg;
    DIR dir={0};snprintf(dir.entry.d_name,256,"file-%d",(int)id);
    dir.entry.d_stat.st_mode=4;
    for(int i=0;i<100;++i) {
        dir.count=0;
        dirent64_bionic *entry=readdir_soloader(&dir);
        pthread_barrier_wait(&barrier);
        assert(entry && !strcmp(entry->d_name,dir.entry.d_name) && entry->d_type==DT_DIR);
        assert(entry->d_reclen==32 && entry->d_ino && !entry->d_off);
        pthread_barrier_wait(&barrier);
    }
    return NULL;
}
int main(void) {
    struct {uint64_t before; bionic_statfs_compat value; uint64_t after[6];} fs;
    memset(&fs,0xa5,sizeof(fs));
    assert(!statfs_soloader("ux0:data/pvz2",&fs.value));
    assert(fs.value.f_bavail*fs.value.f_bsize==128LL<<30);
    assert(fs.before==0xa5a5a5a5a5a5a5a5ULL);
    for(int i=0;i<6;++i)assert(fs.after[i]==0xa5a5a5a5a5a5a5a5ULL);
    volume.free_size=0;assert(!statfs_soloader("ux0:",&fs.value) && !fs.value.f_bavail);
    bionic_statfs_compat unchanged=fs.value;
    device_error=1;assert(statfs_soloader("ux0:",&fs.value)==-1 && errno==EIO);
    assert(!memcmp(&unchanged,&fs.value,sizeof(unchanged)));
    assert(statfs_soloader(NULL,&fs.value)==-1 && errno==EFAULT);
    puts("PASS: storage query writes exactly 88 bytes; 256GB/zero-free-space/device-error scenarios.");

    DIR dir={0};struct {uint64_t a;dirent64_bionic entry;uint64_t b;} guard;
    memset(&guard,0xa5,sizeof(guard));dirent64_bionic *result=(void*)1;
    dir.count=1;assert(!readdir_r_soloader(&dir,&guard.entry,&result) && result==NULL);
    for(unsigned i=0;i<sizeof(guard);++i)assert(((unsigned char*)&guard)[i]==0xa5);
    dir.error=EIO;result=(void*)1;
    assert(readdir_r_soloader(&dir,&guard.entry,&result)==EIO && result==NULL);
    dir.error=0;dir.count=0;memset(dir.entry.d_name,'a',256);
    assert(!readdir_r_soloader(&dir,&guard.entry,&result));
    assert(result==&guard.entry && strlen(result->d_name)==255 && result->d_reclen==280);
    assert(guard.a==0xa5a5a5a5a5a5a5a5ULL && guard.b==0xa5a5a5a5a5a5a5a5ULL);
    pthread_t threads[8];pthread_barrier_init(&barrier,NULL,8);
    for(int i=0;i<8;++i)assert(!pthread_create(&threads[i],NULL,scan_thread,(void*)(intptr_t)i));
    for(int i=0;i<8;++i)pthread_join(threads[i],NULL);
    pthread_barrier_destroy(&barrier);
    puts("PASS: directory EOF/error without uninitialized reads; 255-character names; eight concurrent scans without shared output or per-entry allocation.");

    uint8_t *data;size_t size;
    for(io_error=0;io_error<=7;++io_error) {
        opens=closes=allocations=0;data=(void*)1;size=123;
        int ok=file_load("fixture",&data,&size);
        assert(ok==(io_error==0));
        if(ok){assert(size==16 && data[15]==0x42);free(data);}
        else assert(data==NULL && size==0);
        assert(!allocations && opens==1 && closes==(io_error==1 ? 0 : 1));
    }
    for(io_error=0;io_error<=7;++io_error) {
        opens=closes=0;uint8_t input[16]={0};
        int ok=file_save("fixture",input,sizeof(input));
        assert(ok==(io_error!=1 && io_error!=6 && io_error!=7));
        assert(opens==1 && closes==(io_error==1 ? 0 : 1));
    }
    source_missing=1;assert(rename_soloader("missing.tmp","current-save")==-1 && rename_calls==0);
    source_missing=0;assert(!rename_soloader("new.tmp","current-save") && rename_calls==1);
    puts("PASS: cache read/open/seek/allocation/short-read/close failures clean up; incomplete writes fail; missing save source never enters destructive native rename.");
}
'''
(w/'check.c').write_text(code,encoding='utf-8');exe=w/'check.exe'
subprocess.run(['gcc','-O2','-static','-pthread','-std=gnu11','-Wall','-Wextra','-Werror',
                '-Wno-unused-parameter','-Wno-unused-function',str(w/'check.c'),'-o',str(exe)],check=True,timeout=30)
run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10,check=True)
(w/'result.txt').write_text(run.stdout);print(run.stdout.strip());print(w)
