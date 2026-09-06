"""Render the production error framebuffer in memory; no window or emulator."""
from pathlib import Path
import subprocess,tempfile,shutil,zlib,struct
r=Path(__file__).resolve().parents[1]; s=r/'vita/direct/source'
w=Path(tempfile.mkdtemp(prefix='boot-screen-',dir=r/'out'))
for n in ['ctrl.h','display.h','kernel/sysmem.h','kernel/processmgr.h']:
    p=w/'psp2'/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('')
c=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
typedef int SceUID;
typedef struct {unsigned buttons;} SceCtrlData;
typedef struct {unsigned size;void *base;unsigned pitch,format,width,height;} SceDisplayFrameBuf;
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW 1
#define SCE_DISPLAY_PIXELFORMAT_A8B8G8R8 0
#define SCE_DISPLAY_SETBUF_NEXTFRAME 1
#define SCE_CTRL_CROSS 1
static uint32_t storage[2*1024*1024/4+16];
static unsigned polls, displayed;
static jmp_buf done;
static int sceKernelAllocMemBlock(const char *n,int t,unsigned size,void *p) {assert(size==2*1024*1024);return 1;}
static int sceKernelGetMemBlockBase(int id,void **base) {*base=storage+8;return 0;}
static int sceDisplaySetFrameBuf(const SceDisplayFrameBuf *f,int sync) {
    assert(f->pitch==960&&f->width==960&&f->height==544);displayed++;return 0;
}
static int sceDisplayWaitVblankStart(void) {return 0;}
static int sceCtrlPeekBufferPositive(int port,SceCtrlData *p,int n) {
    p->buttons=(polls++==2)?0:SCE_CTRL_CROSS;return 1;
}
static void sceKernelExitProcess(int status) {assert(status==1);longjmp(done,1);}
#include "utils/boot_screen.c"
int main(void) {
    for(unsigned i=0;i<sizeof(storage)/4;i++) storage[i]=0x55aa55aa;
    if(!setjmp(done)) pvz2_boot_screen("Missing shader compiler: libshacccg.suprx\n\nRun ShaRKBR33D on your Vita to install it.\n\nExpected location:\nur0:data/libshacccg.suprx\nor ur0:data/external/libshacccg.suprx\n\nThen launch the game again.");
    assert(polls==4 && displayed==1);
    for(int i=0;i<8;i++) assert(storage[i]==0x55aa55aa&&storage[8+2*1024*1024/4+i]==0x55aa55aa);
    FILE *f=fopen("screen.rgba","wb");assert(f);fwrite(storage+8,4,960*544,f);fclose(f);
    char long_message[4096];memset(long_message,'W',sizeof(long_message)-1);long_message[4095]=0;
    draw_text(storage+8,32,88,long_message,0xffffffff,464);
    for(int i=0;i<8;i++) assert(storage[i]==0x55aa55aa&&storage[8+2*1024*1024/4+i]==0x55aa55aa);
    puts("PASS: framebuffer format/bounds, long-message clipping, held-button guard, fresh press closes");
}
'''
(w/'test.c').write_text(c,encoding='utf-8')
subprocess.run([shutil.which('gcc'),'-O2','-static','-I',str(w),'-I',str(s),str(w/'test.c'),'-o',str(w/'test.exe')],check=True,timeout=15)
p=subprocess.run([str(w/'test.exe')],cwd=w,capture_output=True,text=True,check=True,timeout=3)
(w/'result.txt').write_text(p.stdout,encoding='utf-8');print(p.stdout.strip())
pixels=(w/'screen.rgba').read_bytes()
def chunk(kind,data): return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
scan=b''.join(b'\0'+pixels[y*960*4:(y+1)*960*4] for y in range(544))
(w/'screen.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',960,544,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(scan))+chunk(b'IEND',b''))
print(w)
