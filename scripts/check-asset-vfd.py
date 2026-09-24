"""Deterministic production VFD concurrency checks, without a Vita."""
from pathlib import Path
import argparse, subprocess, tempfile
r=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=r/'vita/direct/source/reimpl/io.c');args=p.parse_args()
s=args.source.read_text();w=Path(tempfile.mkdtemp(prefix='asset-vfd-',dir=r/'out'))
code=r'''
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#ifndef SSIZE_MAX
#define SSIZE_MAX LONG_MAX
#endif
#define l_info(...) ((void)0)
#define l_warn(...) ((void)0)
#define l_debug(...) ((void)0)
#define l_error(...) ((void)0)
#define sceClibSnprintf snprintf
static pthread_mutex_t gate_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_cond=PTHREAD_COND_INITIALIZER;
static int next_raw=10,block_raw=-1,entered,proceed,live[4096],interrupt_once;
static int block_open,open_entered,open_proceed,open_error,open_errors;
static int fake_open(const char*p,int flags,...){
 (void)flags;pthread_mutex_lock(&gate_lock);
 if(block_open&&!strcmp(p,"slow")){open_entered=1;pthread_cond_broadcast(&gate_cond);while(!open_proceed)pthread_cond_wait(&gate_cond,&gate_lock);}
 if(open_errors){--open_errors;errno=open_error;pthread_mutex_unlock(&gate_lock);return -1;}
 int fd=next_raw++;assert(fd<4096);live[fd]=1;pthread_mutex_unlock(&gate_lock);return fd;
}
static int fake_close(int fd){pthread_mutex_lock(&gate_lock);assert(live[fd]);live[fd]=0;pthread_mutex_unlock(&gate_lock);return 0;}
static ssize_t fake_pread(int fd,void *buf,size_t n,off_t off){
 pthread_mutex_lock(&gate_lock);assert(live[fd]);
 if(fd==block_raw){entered=1;pthread_cond_broadcast(&gate_cond);while(!proceed)pthread_cond_wait(&gate_cond,&gate_lock);assert(live[fd]);}
 if(interrupt_once){interrupt_once=0;pthread_mutex_unlock(&gate_lock);errno=EINTR;return -1;}
 pthread_mutex_unlock(&gate_lock);
 if(n>2)n=2;for(size_t i=0;i<n;i++)((unsigned char*)buf)[i]=(unsigned char)(off+i);return n;
}
#define open fake_open
#define close fake_close
#define pread fake_pread
'''
code+=s[s.index('#define ASSET_VFD_BASE'):s.index('int asset_vfd_fstat(')]
code+=s[s.index('int asset_vfd_close('):s.index('const char *remap_android_path')]
code+=r'''
typedef struct {int fd;ssize_t result;unsigned char bytes[4];} Read;
static void *reader(void*p){Read*r=p;r->result=asset_vfd_read(r->fd,r->bytes,4);return NULL;}
static void *closer(void*p){assert(asset_vfd_close(*(int*)p)==0);return NULL;}
static void *opener(void*p){*(int*)p=asset_vfd_open("slow",256);return NULL;}
static void await_entry(void){pthread_mutex_lock(&gate_lock);while(!entered)pthread_cond_wait(&gate_cond,&gate_lock);pthread_mutex_unlock(&gate_lock);}
static void unblock(void){pthread_mutex_lock(&gate_lock);proceed=1;pthread_cond_broadcast(&gate_cond);pthread_mutex_unlock(&gate_lock);}
int main(void){
 int fd=asset_vfd_open("first",256),other=asset_vfd_open("other",256);assert(fd>=0&&other>=0);
 block_raw=g_asset_vfds[asset_vfd_slot(fd)].raw_fd;
 Read a={.fd=fd},b={.fd=fd};pthread_t t1,t2;
 pthread_create(&t1,NULL,reader,&a);await_entry();pthread_create(&t2,NULL,reader,&b);
 unsigned char bytes[8];assert(asset_vfd_pread(other,bytes,4,21)==4&&bytes[0]==21);
 unblock();pthread_join(t1,NULL);pthread_join(t2,NULL);
 assert(a.result==4&&b.result==4&&a.bytes[0]==0&&b.bytes[0]==4);
 assert(asset_vfd_lseek(fd,0,SEEK_CUR)==8);
 interrupt_once=1;assert(asset_vfd_pread(fd,bytes,8,100)==8&&bytes[7]==107);
 assert(asset_vfd_lseek(fd,0,SEEK_CUR)==8);
 assert(asset_vfd_pread(fd,bytes,SIZE_MAX,254)==2&&bytes[1]==255);
 assert(asset_vfd_lseek(fd,-9,SEEK_CUR)==-1);
 assert(asset_vfd_lseek(fd,(off_t)(((uintmax_t)1<<(sizeof(off_t)*8-1))-1),SEEK_CUR)==-1);
 assert(asset_vfd_read(fd,NULL,1)==-1&&errno==EFAULT);
 /* Close must wait for an in-flight cached read, reject new work, and leave
  * unrelated descriptors usable throughout the wait. */
 entered=proceed=0;a=(Read){.fd=fd};pthread_create(&t1,NULL,reader,&a);await_entry();
 pthread_create(&t2,NULL,closer,&fd);
 for(;;){asset_vfd_lock();int closing=g_asset_vfds[asset_vfd_slot(fd)].used==2;asset_vfd_unlock();if(closing)break;usleep(100);}
 assert(asset_vfd_read(fd,bytes,1)==-1&&errno==EBADF);
 assert(asset_vfd_pread(other,bytes,4,30)==4&&bytes[0]==30);
 unblock();pthread_join(t1,NULL);pthread_join(t2,NULL);assert(a.result==4&&a.bytes[0]==8);
 assert(!asset_vfd_is(fd));fd=asset_vfd_open("replacement",256);
 assert(asset_vfd_read(fd,bytes,4)==4&&bytes[0]==0);
 /* Also pin the virtual slot when its raw FD has been evicted. */
 asset_vfd_trim_cached_fds(0);assert(g_asset_vfds[asset_vfd_slot(fd)].raw_fd<0);
 block_raw=next_raw;entered=proceed=0;a=(Read){.fd=fd};pthread_create(&t1,NULL,reader,&a);await_entry();
 pthread_create(&t2,NULL,closer,&fd);
 for(;;){asset_vfd_lock();int closing=g_asset_vfds[asset_vfd_slot(fd)].used==2;asset_vfd_unlock();if(closing)break;usleep(100);}
 unblock();pthread_join(t1,NULL);pthread_join(t2,NULL);assert(a.result==4&&a.bytes[0]==4);
 asset_vfd_close(other);
 /* One slow open must not block a different asset's read/close. */
 other=asset_vfd_open("other",256);block_open=1;
 int slow=-1;pthread_create(&t1,NULL,opener,&slow);
 pthread_mutex_lock(&gate_lock);while(!open_entered)pthread_cond_wait(&gate_cond,&gate_lock);pthread_mutex_unlock(&gate_lock);
 assert(asset_vfd_pread(other,bytes,4,10)==4&&bytes[0]==10);
 assert(!asset_vfd_close(other));
 asset_vfd_lock();unsigned reserved=0;for(int i=0;i<ASSET_VFD_MAX;i++)reserved+=g_asset_vfds[i].used==3;asset_vfd_unlock();assert(reserved==1);
 pthread_mutex_lock(&gate_lock);open_proceed=1;pthread_cond_broadcast(&gate_cond);pthread_mutex_unlock(&gate_lock);
 pthread_join(t1,NULL);assert(slow>=0);asset_vfd_close(slow);
 /* Repeated failed opens neither return a fake success nor consume slots. */
 for(int i=0;i<600;i++){open_error=ENOENT;open_errors=1;assert(asset_vfd_open("missing",256)==-1&&errno==ENOENT);}
 open_error=EMFILE;open_errors=1;fd=asset_vfd_open("retry",256);assert(fd>=0);asset_vfd_close(fd);
 open_errors=2;assert(asset_vfd_open("failed-retry",256)==-1&&errno==EMFILE);
 /* A previously evicted hot asset is opened once, then remains in the same
  * bounded cache across repeated reads. Closing it releases that descriptor. */
 int many[24];for(int i=0;i<24;i++)many[i]=asset_vfd_open("cache",256);
 assert(g_asset_vfds[asset_vfd_slot(many[0])].raw_fd<0);
 int before_open=next_raw;
 for(int i=0;i<500;i++)assert(asset_vfd_pread(many[0],bytes,4,11)==4&&bytes[0]==11);
 assert(next_raw==before_open+1&&g_asset_vfd_live_raw<=ASSET_VFD_RAW_CACHE_SOFT_MAX);
 for(int i=0;i<24;i++)asset_vfd_close(many[i]);
 for(int i=0;i<ASSET_VFD_MAX;i++)assert(!g_asset_vfds[i].used);
 for(int i=10;i<next_raw;i++)assert(!live[i]);
 puts("PASS: concurrent shared-offset reads, independent pread, cached/evicted close races, no leaked FDs, EINTR/short reads, oversized counts and seek overflow");
 puts("PASS: slow open permits unrelated read/close; 600 failed opens leak no slots; exhaustion retries once; evicted asset re-caches once across 500 reads within 16-handle budget");
}
'''
(w/'check.c').write_text(code)
subprocess.run(['gcc','-std=gnu11','-O2','-static','-pthread',str(w/'check.c'),'-o',str(w/'check.exe')],check=True)
subprocess.run([str(w/'check.exe')],check=True,timeout=15)
print(w)
