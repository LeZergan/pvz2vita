"""Actual graphics-init and archive-cache regression checks, no renderer/UI."""
from pathlib import Path
import subprocess, tempfile, shutil, zlib, os
r=Path(__file__).resolve().parents[1];s=r/'vita/direct/source'
w=Path(tempfile.mkdtemp(prefix='loading-check-',dir=r/'out'))
def build(name, code, extra=()):
 p=w/(name+'.cpp');p.write_text(code,encoding='utf-8')
 exe=w/(name+'.exe')
 subprocess.run([shutil.which('g++'),'-O2','-std=c++17','-static','-pthread','-I',str(w),'-I',str(s),str(p),*map(str,extra),'-o',str(exe)],check=True,timeout=25)
 return exe
def run(exe,*args):
 result=subprocess.run([str(exe),*args],cwd=w,timeout=15,capture_output=True,text=True)
 if result.returncode: print(result.stdout,result.stderr);result.check_returncode()
 print(result.stdout.strip());return result.stdout
gl=(s/'utils/glutil.c').read_text(encoding='utf-8')
init=gl[gl.index('static void pvz2_initialize_vitagl('):gl.index('void gl_init() {')]
graphics=build('graphics',r'''
#include <cassert>
#include <cstdio>
#include <csetjmp>
typedef unsigned char GLboolean;
typedef void SceGxmContext;
#define RS_NATIVE_W 960
#define RS_NATIVE_H 544
#define SCE_GXM_MULTISAMPLE_NONE 0
SceGxmContext *gxm_context;
void *gxm_color_surfaces_addr[3];
static bool fallback, missing;
static int ready, calls;
static jmp_buf failed;
static GLboolean vglInitExtended(int pool,int width,int height,int threshold,int aa) {
 assert(pool==0&&width==960&&height==544&&threshold==32*1024*1024&&aa==0);
 calls++;gxm_context=missing?nullptr:(void*)1;
 gxm_color_surfaces_addr[0]=(void*)2;gxm_color_surfaces_addr[1]=(void*)3;
 return fallback;
}
static void fatal_error(const char *,...) {longjmp(failed,1);}
static void pvz2_dialog_graphics_ready(void) {ready++;}
static void telemetry_log(const char *,const char *,...) {}
'''+init+r'''
int main() {
 fallback=false;pvz2_initialize_vitagl(32);assert(ready==1&&calls==1);
 fallback=true;pvz2_initialize_vitagl(32);assert(ready==2&&calls==2);
 missing=true;if(!setjmp(failed)) {pvz2_initialize_vitagl(32);assert(false);}
 assert(ready==2&&calls==3);
 puts("PASS: real graphics-init code accepts normal zero and fallback one; missing resources stop before ready");
}
''')
evidence=run(graphics)
for n in ('io/stat.h','kernel/processmgr.h'):
 p=w/'psp2'/n;p.parent.mkdir(parents=True,exist_ok=True)
 p.write_text('#include <direct.h>\n#define sceIoMkdir(p,m) _mkdir(p)\n' if n.startswith('io') else '#include <stdint.h>\nuint64_t sceKernelGetSystemTimeWide(void);\n')
miniz=r/'vita/direct/third_party/miniz'
cache=build('cache',r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <pthread.h>
#include <vector>
#include <string>
#define DATA_PATH "./"
#define PVZ2_INDEX_PATH "index.idx"
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_STDIO
#define DEBUG_SOLOADER
#define PVZ2_VERBOSE_RUNTIME_INFO
static const char *archive;
static unsigned bundled, scans, reused, extracted;
extern "C" void telemetry_log(const char *,const char *fmt,...) {
 if(strstr(fmt,"bundled index:"))bundled++;
 if(strstr(fmt,"reused verified"))reused++;
}
extern "C" void _log_print(int,const char *fmt,...) {
 if(strstr(fmt,"files indexed"))scans++;
 if(strstr(fmt,"extracted="))extracted++;
}
extern "C" const char *pvz2_obb_path(void) {return archive;}
uint64_t sceKernelGetSystemTimeWide(void) {return 0;}
#include "reimpl/rsb_index_vita.cpp"
extern "C" int pthread_create_soloader(pthread_t *t,const pthread_attr_t_bionic *,void *(*fn)(void *),void *p) {return pthread_create(t,nullptr,fn,p);}
static void write_index(const std::vector<uint8_t> &b) {
 FILE *f=fopen("index.idx","wb");assert(f);assert(fwrite(b.data(),1,b.size(),f)==b.size());assert(!fclose(f));
}
int main(int argc,char **argv) {
 assert(argc==3);archive=argv[1];
 assert(load_index());assert(bundled==1&&scans==0&&g_entries.size()==3710);
 if(!strcmp(argv[2],"full")) {
  auto expected=g_entries;
  assert(!rename("index.idx","index.backup"));assert(load_index());assert(scans==1);
  assert(expected.size()==g_entries.size());
  for(const auto &p:expected) {
   const auto &a=p.second;const auto &b=g_entries.at(p.first);
   assert(a.offset==b.offset&&a.size==b.size&&a.compressed==b.compressed&&a.block_offset==b.block_offset&&
          a.block_size==b.block_size&&a.unpacked_size==b.unpacked_size&&a.within_block==b.within_block);
  }
  assert(!rename("index.backup","index.idx"));
  FILE *f=fopen("index.idx","rb");fseek(f,0,SEEK_END);std::vector<uint8_t> original(ftell(f));rewind(f);
  assert(fread(original.data(),1,original.size(),f)==original.size());fclose(f);
  std::vector<uint8_t> head(256);f=fopen(archive,"rb");assert(fread(head.data(),1,256,f)==256);fclose(f);
  const uint32_t file_size=656855040;
  for(int test=0;test<6;test++) {
   auto corrupt=original;
   if(test==0)corrupt[40]^=1;
   if(test==1)corrupt.resize(100);
   if(test==2)corrupt[12]^=1;
   if(test==3) {memset(corrupt.data()+64,0xff,4);uint32_t crc=mz_crc32(0,corrupt.data()+32,corrupt.size()-32);memcpy(corrupt.data()+24,&crc,4);}
   if(test==4)memset(corrupt.data()+20,0xff,4);
   if(test==5)corrupt.push_back(1);
   write_index(corrupt);assert(!load_bundled_index(file_size,head));assert(g_entries.size()==3710);
  }
  write_index(original);assert(load_bundled_index(file_size,head));
  puts("PASS: all 3710 bundled records equal actual OBB parser; bad checksum/truncation/binding/lengths/trailing bytes rejected");
 }
 g_tried=true;g_loaded=true;
 uint64_t offset;uint32_t size;
 const char *path=vita_rsb_locate("images/768/initial/effects/load_icon_front/load_icon_front.pam",&offset,&size);
 assert(path&&offset==0x5000&&size==322);
 assert(cached_block_valid(path,g_entries.at(normalize("images/768/initial/effects/load_icon_front/load_icon_front.pam"))));
 if(!strcmp(argv[2],"warm"))assert(reused==1&&extracted==0);else assert(reused==0&&extracted==1);
 assert(vita_rsb_locate("images/768/initial/effects/load_icon_back/load_icon_back.pam",&offset,&size));
 assert(offset==0x2000&&size==10949&&reused+extracted==1);
 // Different archive bytes, even at identical block offset/length, reject reuse.
 const char *old_archive=archive;archive="other-source.bin";
 FILE *f=fopen(archive,"wb");assert(f);fclose(f);
 assert(!cached_block_valid(path,g_entries.at(normalize("images/768/initial/effects/load_icon_front/load_icon_front.pam"))));
 archive=old_archive;
 printf("PASS: resource cache reused=%u extracted=%u; shared block, byte ranges, changed-source rejection\n",reused,extracted);
}
''',[miniz/'miniz.c',miniz/'miniz_tinfl.c',miniz/'miniz_tdef.c'])
obb=r/'.codex_tmp_vita_data/zip/com.ea.game.pvz2_row/main.147.com.ea.game.pvz2_row.obb'
shutil.copy2(r/'vita/direct/extras/rsb452.idx',w/'index.idx')
evidence+=run(cache,str(obb),'full')
evidence+=run(cache,str(obb),'warm')
cached=w/'cache/rsb452/00273000.bin';b=bytearray(cached.read_bytes());b[17]^=0x80;cached.write_bytes(b)
evidence+=run(cache,str(obb),'repair')
cached.write_bytes(cached.read_bytes()[:10]);evidence+=run(cache,str(obb),'repair')
with obb.open('rb') as f:f.seek(0x273000);expected=zlib.decompress(f.read(0x3000))
assert cached.read_bytes()==expected
evidence+='PASS: all 32768 extracted/repaired bytes equal independent Python zlib output\n'
(w/'result.txt').write_text(evidence,encoding='utf-8');print(w)
