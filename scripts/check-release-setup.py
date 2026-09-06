"""Small native checks of real setup/migration/path code. No emulator or UI."""
from pathlib import Path
import subprocess, tempfile, shutil

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'vita/direct/source'
WORK = Path(tempfile.mkdtemp(prefix='release-setup-', dir=ROOT/'out'))
def build(name, code):
    path = WORK/(name+'.c'); path.write_text(code, encoding='utf-8')
    exe = WORK/(name+'.exe')
    subprocess.run([shutil.which('gcc'), '-O2', '-static', '-pthread', '-I', str(WORK),
                    '-I', str(SRC), str(path), '-o', str(exe)], check=True, timeout=20)
    return exe
def run(exe, *args, cwd=WORK):
    p = subprocess.run([str(exe), *map(str,args)], cwd=cwd, text=True,
                       capture_output=True, timeout=8, check=True)
    return p.stdout

layout = build('layout', r'''
#include <direct.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
static int remaining = -1;
static int checked_rename(const char *a, const char *b) {
    if (remaining == 0) { errno=EIO; return -1; }
    if (remaining > 0) --remaining;
    return rename(a,b);
}
#define rename checked_rename
#define mkdir(path, mode) _mkdir(path)
#define GAME_DATA_PATH "pvz2/"
#define DATA_PATH "pvz2/userdata/"
#include "utils/data_layout.c"
int main(int argc, char **argv) {
    char error[1024];
    if (argc>1) remaining=atoi(argv[1]);
    int ok=pvz2_prepare_userdata(error,sizeof(error));
    printf("%d\n%s",ok,ok ? "" : error);
}
''')
def case(name):
    d=WORK/name; (d/'pvz2').mkdir(parents=True)
    for f in ('libPVZ2.so','game.obb','main.147.com.ea.game.pvz2_row.obb'):
        (d/'pvz2'/f).write_bytes(f.encode())
    return d
d=case('old-install')
(d/'pvz2/No_Backup').mkdir(); (d/'pvz2/No_Backup/profile').write_bytes(b'SAVE-KEEP')
(d/'pvz2/config.kv').write_bytes(b'age\t42\n'); (d/'pvz2/loader.log').write_bytes(b'OLD-LOG')
assert run(layout,cwd=d).startswith('1\n')
assert (d/'pvz2/userdata/No_Backup/profile').read_bytes()==b'SAVE-KEEP'
assert (d/'pvz2/userdata/config.kv').read_bytes()==b'age\t42\n'
assert (d/'pvz2/userdata/loader.log').read_bytes()==b'OLD-LOG'
assert set(p.name for p in (d/'pvz2').iterdir())=={'libPVZ2.so','game.obb','main.147.com.ea.game.pvz2_row.obb','userdata'}
assert run(layout,cwd=d).startswith('1\n')
d=case('interrupted')
for i in range(4): (d/f'pvz2/file{i}').write_bytes(bytes([i]))
assert run(layout,2,cwd=d).startswith('0\n')
assert not (d/'pvz2/userdata/.layout-v1').exists()
assert run(layout,cwd=d).startswith('1\n')
for i in range(4): assert (d/f'pvz2/userdata/file{i}').read_bytes()==bytes([i])
d=case('conflict'); (d/'pvz2/userdata').mkdir()
(d/'pvz2/config.kv').write_bytes(b'OLD'); (d/'pvz2/userdata/config.kv').write_bytes(b'NEW')
assert 'Two copies' in run(layout,cwd=d)
assert (d/'pvz2/config.kv').read_bytes()==b'OLD'
assert (d/'pvz2/userdata/config.kv').read_bytes()==b'NEW'
assert not (d/'pvz2/userdata/.layout-v1').exists()
d=case('new-install'); assert run(layout,cwd=d).startswith('1\n')
print('PASS: real migration, save/log bytes, repeat boot, interrupted resume, conflicts, fresh setup')

# Run the actual path mapper with two threads deliberately overlapping its
# returned pointers. A shared static buffer fails this test deterministically.
s=(SRC/'reimpl/io.c').read_text(encoding='utf-8')
mapper=s[s.index('const char *remap_android_path('):s.index('static void ensure_parent_dirs_for_path(')]
paths=build('paths', r'''
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#define DATA_PATH "ux0:data/pvz2/userdata/"
#define GAME_DATA_PATH "ux0:data/pvz2/"
#define sceClibSnprintf snprintf
static int str_starts_with(const char *a,const char *b) { return !strncmp(a,b,strlen(b)); }
''' + mapper + r'''
static pthread_barrier_t barrier;
static void *worker(void *p) {
    const char *name=p;
    for (int i=0;i<1000;i++) {
        const char *mapped=remap_android_path(name);
        pthread_barrier_wait(&barrier);
        char expected[128]; snprintf(expected,sizeof(expected),"%s%s",DATA_PATH,name+1);
        assert(!strcmp(mapped,expected));
        pthread_barrier_wait(&barrier);
    }
    return NULL;
}
int main(void) {
    assert(!strcmp(remap_android_path("/No_Backup/profile"),DATA_PATH "No_Backup/profile"));
    assert(!strcmp(remap_android_path(GAME_DATA_PATH "No_Backup/profile"),DATA_PATH "No_Backup/profile"));
    assert(!strcmp(remap_android_path(DATA_PATH "No_Backup/profile"),DATA_PATH "No_Backup/profile"));
    assert(!strcmp(remap_android_path("/" GAME_DATA_PATH "game.obb"),GAME_DATA_PATH "game.obb"));
    assert(!strcmp(remap_android_path(DATA_PATH GAME_DATA_PATH "game.obb"),GAME_DATA_PATH "game.obb"));
    assert(!strcmp(remap_android_path("/dev/urandom"),"/dev/urandom"));
    assert(!strcmp(remap_android_path("/sdcard/Android/data/com.ea.game.pvz2_row/files/No_Backup/a"),DATA_PATH "No_Backup/a"));
    pthread_t a,b; pthread_barrier_init(&barrier,NULL,2);
    pthread_create(&a,NULL,worker,"/one"); pthread_create(&b,NULL,worker,"/two");
    pthread_join(a,NULL); pthread_join(b,NULL);
    puts("PASS: old/new save paths, absolute/doubled OBB, Android paths, 2000 concurrent translations");
}
''')
print(run(paths).strip())

(WORK/'utils').mkdir(); (WORK/'psp2/io').mkdir(parents=True)
(WORK/'utils/utils.h').write_text('#include <stdbool.h>\nbool module_loaded(const char *);\nbool file_exists(const char *);\n')
(WORK/'psp2/io/stat.h').write_text('int sceIoMkdir(const char *, int);\n')
boot=build('boot', r'''
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
static int plugin=1,shader=1,lib=1,obb=1,legacy=0,wrong_size=0,wrong_header=0,wrong_fp=0,write_fail=0,close_fail=0;
typedef struct {int kind;long pos;} FakeFile;
static FakeFile files[4];
static FakeFile *fake_open(const char *p,const char *m) {
    int kind=strstr(p,"libPVZ2.so")?1:strstr(p,".obb")?2:3;
    if ((kind==1&&!lib)||(kind==2&&!obb&&!legacy)||(kind==3&&write_fail)) {errno=EACCES;return NULL;}
    if(kind==3) assert(strstr(p,".pvz2-write-check.tmp"));
    files[kind]=(FakeFile){kind,0}; return &files[kind];
}
static int fake_seek(FakeFile *f,long pos,int whence) { f->pos=whence==SEEK_END?(f->kind==1?18198492:656855040)-wrong_size:pos;return 0; }
static long fake_tell(FakeFile *f) {return f->pos;}
static void fake_rewind(FakeFile *f) {f->pos=0;}
static size_t fake_read(void *p,size_t size,size_t count,FakeFile *f) {
    memset(p,0,size*count);
    if(f->pos==0) {
        if(f->kind==1) {unsigned char h[]={127,'E','L','F',1,1,1};memcpy(p,h,sizeof(h));((char*)p)[18]=40;}
        else memcpy(p,"1bsr\x04\x00\x00\x00",8);
        if(wrong_header) ((char*)p)[0]=0;
    } else {uint64_t v=f->pos==0xcc033c?UINT64_C(0xE24DD084E92D4FF0):UINT64_C(0xE59F1010E59F0010); if(wrong_fp) v=0;memcpy(p,&v,8);}
    return count;
}
static size_t fake_write(const void *p,size_t size,size_t n,FakeFile *f) {return n;}
static int fake_close(FakeFile *f) {if(f->kind==3&&close_fail){errno=ENOSPC;return -1;}return 0;}
bool module_loaded(const char *p) {return plugin;}
bool file_exists(const char *p) {return strstr(p,"shacccg")?shader:strstr(p,"main.147")?legacy:obb;}
int sceIoMkdir(const char *p,int m) {return 0;}
#define FILE FakeFile
#define fopen fake_open
#define fseek fake_seek
#define ftell fake_tell
#define rewind fake_rewind
#define fread fake_read
#define fwrite fake_write
#define fclose fake_close
#define DATA_PATH "ux0:data/pvz2/userdata/"
#define GAME_DATA_PATH "ux0:data/pvz2/"
#include "utils/boot_check.c"
int main(void) {
    char error[1024];
    assert(pvz2_boot_check(error,sizeof(error)));
    plugin=0; assert(!pvz2_boot_check(error,sizeof(error))&&strstr(error,"kubridge"));plugin=1;
    shader=0; assert(!pvz2_boot_check(error,sizeof(error))&&strstr(error,"libshacccg"));shader=1;
    lib=0; assert(!pvz2_boot_check(error,sizeof(error))&&strstr(error,"libPVZ2.so"));lib=1;
    obb=0; assert(!pvz2_boot_check(error,sizeof(error))&&strstr(error,"game.obb"));
    legacy=1; assert(pvz2_boot_check(error,sizeof(error))&&strstr(pvz2_obb_path(),"main.147"));obb=1;
    assert(pvz2_boot_check(error,sizeof(error))&&strstr(pvz2_obb_path(),"game.obb"));
    wrong_size=1;assert(!pvz2_boot_check(error,sizeof(error))&&strstr(error,"incomplete"));wrong_size=0;
    wrong_header=1;assert(!pvz2_boot_check(error,sizeof(error))&&strstr(error,"damaged"));wrong_header=0;
    wrong_fp=1;assert(!pvz2_boot_check(error,sizeof(error)));wrong_fp=0;
    write_fail=1;assert(!pvz2_boot_check(error,sizeof(error))&&strstr(error,"Cannot write"));write_fail=0;
    close_fail=1;assert(!pvz2_boot_check(error,sizeof(error))&&strstr(error,"Cannot write"));close_fail=0;
    struct {char error[8];char guard[8];} small;memset(&small,0x55,sizeof(small));
    plugin=0;assert(!pvz2_boot_check(small.error,sizeof(small.error)));assert(small.error[7]==0);
    for(int i=0;i<8;i++) assert(small.guard[i]==0x55);
    puts("PASS: missing plugins/files, legacy/simple OBB, bad size/header/code, write/flush failure, bounded errors");
}
''')
print(run(boot).strip())
(WORK/'result.txt').write_text('PASS: setup, real migration/resume/conflicts, concurrent paths.\n',encoding='utf-8')
print('Evidence:',WORK)
