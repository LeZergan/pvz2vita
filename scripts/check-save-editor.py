"""Run production RTON edits on copies only, with I/O failure/recovery checks."""
from pathlib import Path
import subprocess, tempfile, shutil, struct, json, hashlib
ROOT=Path(__file__).resolve().parents[1]
w=Path(tempfile.mkdtemp(prefix='save-editor-',dir=ROOT/'out'))
src=ROOT/'vita/direct/source'
code=r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#ifdef _WIN32
#include <io.h>
#endif
#include <string>
static int fault;
static unsigned sync_calls;
static std::string current_path;
namespace std {
static int editor_rename(const char *a,const char *b) {
 if(((fault==2||fault==4) && strstr(a,".editor-tmp")) ||
    (fault==3 && strstr(b,".editor-old")) || (fault==4 && strstr(a,".editor-old"))){errno=EIO;return -1;}
 return std::rename(a,b);
}
}
static int editor_sync(int fd) {
 ++sync_calls;
 if(fault==5&&sync_calls==2){
  // Simulate another writer after the temporary edit is written, before commit.
  FILE*f=fopen(current_path.c_str(),"ab");assert(f);fputc('!',f);assert(!fclose(f));
 }
 if(fault==6&&sync_calls==2)return -1;
#ifdef _WIN32
 return fault==1?-1:_commit(fd);
#else
 return fault==1?-1:fsync(fd);
#endif
}
#define rename editor_rename
#define fsync editor_sync
#include "utils/save_editor.cpp"
#undef rename
#undef fsync
int main(int argc,char **argv) {
 assert(argc>=3);current_path=argv[2];Pvz2SaveProfile p[16];char error[256]={0};
 if(!strcmp(argv[1],"recover"))return pvz2_save_recover(argv[2],error,sizeof(error))?0:1;
 if(!strcmp(argv[1],"batch")) {
  assert(argc==8);assert(pvz2_save_read(argv[2],p,16,error,sizeof(error))==2);
  p[0].coins=strtoul(argv[3],nullptr,10);p[0].gems=strtoul(argv[4],nullptr,10);
  p[1].coins=strtoul(argv[5],nullptr,10);p[1].gems=strtoul(argv[6],nullptr,10);fault=atoi(argv[7]);
  if(!pvz2_save_apply_batch(argv[2],p,2,3,error,sizeof(error))){puts(error);return 2;}
 }
 if(!strcmp(argv[1],"apply")) {
  assert(argc==7);fault=atoi(argv[6]);
  int ok=pvz2_save_apply(argv[2],atoi(argv[3]),strtoul(argv[4],nullptr,10),strtoul(argv[5],nullptr,10),error,sizeof(error));
  puts(error);if(!ok)return 2;
 }
 int count=pvz2_save_read(argv[2],p,16,error,sizeof(error));
 if(!count){puts(error);return 3;}
 for(int i=0;i<count;++i)printf("%u %u\n",p[i].coins,p[i].gems);
 return 0;
}
'''
(w/'test.cpp').write_text(code)
exe=w/'test.exe'
subprocess.run(['g++','-O2','-std=c++17','-static','-I',str(src),str(w/'test.cpp'),'-o',str(exe)],check=True,timeout=30)
def run(mode,path,*args,ok=True):
 r=subprocess.run([str(exe),mode,str(path),*map(str,args)],capture_output=True,text=True,timeout=5)
 assert (r.returncode==0)==ok,(r.stdout,r.stderr,r.returncode)
 return r.stdout
def string(s):
 b=s.encode();assert len(b)<128;return b'\x81'+bytes([len(b)])+b
def num(n):return b'\x20'+struct.pack('<i',n)
def obj(items):return b'\x85'+b''.join(string(k)+v for k,v in items)+b'\xff'
def profile(name,c,g):return obj([('objclass',string('PlayerInfo')),('objdata',obj([('n',string(name)),('c',c),('g',g),('nested',obj([('c',num(991)),('g',num(992))])),('plant',b'\x86\xfd\x02'+num(1)+num(13)+b'\xfe')]))])
def document(players):return b'RTON\x01\0\0\0'+string('objects')+b'\x86\xfd'+bytes([len(players)])+b''.join(players)+b'\xfe\xffDONE'
original=document([profile('One',b'\x21',b'\x24\x03'),profile('Two',num(234),num(456))])
path=w/'pp.dat';path.write_bytes(original)
assert run('read',path)=='0 3\n234 456\n'
run('apply',path,1,123456789,987654321,0)
expected=document([profile('One',b'\x21',b'\x24\x03'),profile('Two',num(123456789),num(987654321))])
assert path.read_bytes()==expected # Every unrelated byte remains identical.
assert Path(str(path)+'.backup-0').read_bytes()==original
run('apply',path,0,999999999,0,0)
assert Path(str(path)+'.backup-1').read_bytes()==expected
before=path.read_bytes()
run('apply',path,0,42,43,1,ok=False);assert path.read_bytes()==before
run('apply',path,0,42,43,2,ok=False);assert path.read_bytes()==before
run('apply',path,0,1000000000,43,0,ok=False);assert path.read_bytes()==before
run('apply',path,17,42,43,0,ok=False);assert path.read_bytes()==before
# An interrupted transaction recovers the original before game startup.
old=Path(str(path)+'.editor-old');path.rename(old)
run('recover',path);assert path.read_bytes()==before and not old.exists()
# Both staged players commit once; failure never commits just the first one.
batch=w/'batch.dat';batch.write_bytes(original)
run('batch',batch,100,200,300,400,2,ok=False);assert batch.read_bytes()==original
run('batch',batch,100,200,300,400,1,ok=False);assert batch.read_bytes()==original
run('batch',batch,100,200,300,1000000000,0,ok=False);assert batch.read_bytes()==original
run('batch',batch,100,200,300,400,0)
assert batch.read_bytes()==document([profile('One',num(100),num(200)),profile('Two',num(300),num(400))])
assert all(p.read_bytes()==original for p in w.glob('batch.dat.backup-*'))
# Exercise additional real transaction stages and preserve recoverable originals.
for fault in [3,4,5,6]:
 transaction=w/f'fault-{fault}.dat';transaction.write_bytes(original)
 run('apply',transaction,0,42,43,fault,ok=False)
 transaction_old=Path(str(transaction)+'.editor-old')
 if fault==4:
  assert not transaction.exists() and transaction_old.read_bytes()==original
  run('recover',transaction);assert transaction.read_bytes()==original and not transaction_old.exists()
 elif fault==5:assert transaction.read_bytes()==original+b'!'
 else:assert transaction.read_bytes()==original
 assert Path(str(transaction)+'.backup-0').read_bytes()==original
# A blocked temporary path and exhausted backup slots fail without replacing data.
blocked=w/'blocked.dat';blocked.write_bytes(original)
blocked_temp=Path(str(blocked)+'.editor-tmp');blocked_temp.mkdir()
(blocked_temp/'keep').write_bytes(b'keep')
assert 'write failed' in run('apply',blocked,0,42,43,0,ok=False)
assert blocked.read_bytes()==original and (blocked_temp/'keep').read_bytes()==b'keep'
full=w/'full.dat';full.write_bytes(original)
for i in range(1000):Path(str(full)+f'.backup-{i}').write_bytes(b'keep')
assert 'Backup slots full' in run('apply',full,0,42,43,0,ok=False)
assert full.read_bytes()==original and all(p.read_bytes()==b'keep' for p in w.glob('full.dat.backup-*'))
# Existing balances above the editable range are legal and not rewritten.
high=w/'high.dat';high_data=document([profile('Rich',num(2000000000),num(1500000000))]);high.write_bytes(high_data)
run('apply',high,0,2000000000,1500000000,0);assert high.read_bytes()==high_data
assert not Path(str(high)+'.backup-0').exists()
run('apply',high,0,123,1500000000,0)
assert high.read_bytes()==document([profile('Rich',num(123),num(1500000000))])
# Duplicate editable fields, excessive depth, invalid cache refs, encrypted input
# and every truncation of a valid synthetic document must fail without writes.
bad=[b'\x10'+original,original.replace(string('g')+b'\x24\x03',string('g')+b'\x24\x03'+string('g')+b'\x21'),
     original.replace(string('n')+string('One'),string('n')+b'\x91\x7f'),
     b'RTON\x01\0\0\0'+string('deep')+b'\x85'+(string('x')+b'\x85')*70+b'\xff'*72+b'DONE']
bad.extend(original[:i] for i in range(len(original)))
for i,b in enumerate(bad):
 path.write_bytes(b);run('read',path,ok=False);run('apply',path,0,1,2,0,ok=False);assert path.read_bytes()==b
real=ROOT/'out/save-check-20260905-210533/No_Backup/pp.dat'
if real.exists():
 real_hash=hashlib.sha256(real.read_bytes()).hexdigest()
 path.write_bytes(real.read_bytes());assert run('read',path)=='0 0\n'
 run('apply',path,0,12345,6789,0)
 assert run('read',path)=='12345 6789\n'
 # Exact original spans for c/g in this preserved zero-currency save.
 baseline=real.read_bytes();modified=baseline.replace(b'\x90\x01c\x21',b'\x90\x01c'+num(12345)).replace(b'\x90\x01g\x21',b'\x90\x01g'+num(6789))
 assert path.read_bytes()==modified
 assert hashlib.sha256(real.read_bytes()).hexdigest()==real_hash
 print('PASS: preserved real 4.5.2 save/string references; source save SHA256 unchanged')
print('PASS: atomic batch; exact edits/backups; high untouched balances; backup/temp/stage/commit/rollback failures; recovery; concurrent-write detection; blocked paths/full backup slots; malformed/truncated saves rejected')
(w/'result.json').write_text(json.dumps({'passed':True,'malformed_cases':len(bad),'transaction_faults':[1,2,3,4,5,6],'source_save_unchanged':True},indent=2))
print(w)
