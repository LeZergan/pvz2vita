"""Production parser on synthetic mods with changed lengths, names and offsets."""
from pathlib import Path
import subprocess,tempfile,struct
r=Path(__file__).resolve().parents[1];w=Path(tempfile.mkdtemp(prefix='obb-mods-',dir=r/'out'));src=r/'vita/direct/source'
for name in ['io/stat.h','kernel/processmgr.h']:
 p=w/'psp2'/name;p.parent.mkdir(parents=True,exist_ok=True)
 p.write_text('#ifdef _WIN32\n#include <direct.h>\n#define sceIoMkdir(p,m) _mkdir(p)\n#else\n#include <sys/stat.h>\n#define sceIoMkdir(p,m) mkdir(p,m)\n#endif\n' if name.startswith('io') else '#include <stdint.h>\nuint64_t sceKernelGetSystemTimeWide(void);\n')
code=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#define DATA_PATH "./"
static const char *archive;
extern "C" const char *pvz2_obb_path(void){return archive;}
extern "C" void telemetry_log(const char*,const char*,...){}
extern "C" void _log_print(int,const char*,...){}
uint64_t sceKernelGetSystemTimeWide(void){return 0;}
#include "reimpl/rsb_index_vita.cpp"
extern "C" int pthread_create_soloader(pthread_t*t,const pthread_attr_t_bionic*,void*(*fn)(void*),void*p){return pthread_create(t,nullptr,fn,p);}
int main(int argc,char**argv){
 assert(argc==3);archive=argv[1];
 if(!strcmp(argv[2],"REJECT")){assert(!load_index()&&g_entries.empty());puts("PASS: malformed mod rejected without hang or partial index");return 0;}
 assert(load_index());
 if(!strcmp(argv[2],"STOCK")){assert(g_entries.size()>1000);printf("PASS: stock archive still indexes %zu resource names\n",g_entries.size());return 0;}
 assert(g_entries.size()==1);
 auto e=g_entries.at(argv[2]);assert(e.size==7);
 FILE*f=fopen(archive,"rb");assert(f);assert(!fseek(f,(long)e.offset,SEEK_SET));char b[7];assert(fread(b,1,7,f)==7);fclose(f);assert(!memcmp(b,"MODDATA",7));
 puts("PASS: modified archive resource name/offset/size used without stock archive binding");
}
'''
(w/'test.cpp').write_text(code);exe=w/'test.exe';mz=r/'vita/direct/third_party/miniz'
subprocess.run(['g++','-O2','-static','-std=c++17','-pthread','-I',str(w),'-I',str(src),str(w/'test.cpp'),str(mz/'miniz.c'),str(mz/'miniz_tinfl.c'),str(mz/'miniz_tdef.c'),'-o',str(exe)],check=True,timeout=30)
def archive(name,within,length):
 b=bytearray(length);b[:8]=b'1bsr\x04\0\0\0';struct.pack_into('<3I',b,0x28,1,0x100,0x88);struct.pack_into('<I',b,0x180,0x200)
 table=b''.join(bytes([ch,0,0,0]) for ch in name.encode())+struct.pack('<4I',0,0,within,7)
 b[0x200:0x208]=b'pgsr\x04\0\0\0';struct.pack_into('<I',b,0x218,0x200);struct.pack_into('<2I',b,0x248,len(table),0x60)
 b[0x260:0x260+len(table)]=table;b[0x400+within:0x407+within]=b'MODDATA';return bytes(b)
for i,(name,within,length) in enumerate([('PROPERTIES\\MOD_ONE.RTON',0,4096),('PROPERTIES\\MOD_TWO.RTON',700,4096),('PROPERTIES\\MOD_THREE.RTON',1500,8192)]):
 p=w/f'mod-{i}.obb';p.write_bytes(archive(name,within,length))
 # A stale original index cannot be consumed, even when header and size match.
 (w/'rsb452.idx').write_bytes(b'POISONED OFFSETS')
 result=subprocess.run([str(exe),str(p),name],cwd=w,check=True,capture_output=True,text=True,timeout=5);print(result.stdout.strip())
base=archive('PROPERTIES\\BAD.RTON',0,4096)
bad=[]
def change(offset,value):
 b=bytearray(base);struct.pack_into('<I',b,offset,value);return b
# Cyclic trie, invalid branch target, excessive directory, escaped byte range.
b=bytearray(base);b[0x261:0x264]=b'\x01\0\0';bad.append(b)
b=bytearray(base);b[0x261:0x264]=b'\xff\xff\xff';bad.append(b)
bad.append(change(0x248,4*1024*1024+4))
bad.append(change(0x260+len('PROPERTIES\\BAD.RTON')*4+12,9999999))
bad.append(change(0x260+len('PROPERTIES\\BAD.RTON')*4+4,7))
for i,data in enumerate(bad):
 p=w/f'bad-{i}.obb';p.write_bytes(data)
 result=subprocess.run([str(exe),str(p),'REJECT'],cwd=w,check=True,capture_output=True,text=True,timeout=5);print(result.stdout.strip())
stock=r/'game/main.147.com.ea.game.pvz2_row.obb'
if stock.exists():
 result=subprocess.run([str(exe),str(stock),'STOCK'],cwd=w,check=True,capture_output=True,text=True,timeout=30);print(result.stdout.strip())
print(w)
