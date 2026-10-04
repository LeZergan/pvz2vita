"""Drive the production software boot settings screen with scripted input.

No emulator/device. Render actual pixels, check staged edits and Done/cancel.
"""
from pathlib import Path
import subprocess,tempfile,shutil,os
from PIL import Image
r=Path(__file__).resolve().parents[1];w=Path(tempfile.mkdtemp(prefix='port-menu-',dir=r/'out'));src=r/'vita/direct/source'
for path in ['ctrl.h','display.h','kernel/sysmem.h','kernel/processmgr.h','kernel/threadmgr.h']:
 p=w/'psp2'/path;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('#include "menu_stubs.h"\n')
stub=r'''
#ifndef MENU_STUBS
#define MENU_STUBS
#include <stdint.h>
typedef int SceUID;
typedef struct {unsigned buttons;} SceCtrlData;
typedef struct {unsigned size;void *base;unsigned pitch,pixelformat,width,height;} SceDisplayFrameBuf;
#define SCE_CTRL_DOWN 0x00000040u
#define SCE_CTRL_CROSS 0x00004000u
#define SCE_CTRL_LTRIGGER 0x00000100u
#define SCE_CTRL_RTRIGGER 0x00000200u
#define SCE_CTRL_L1 0x00000400u
#define SCE_CTRL_R1 0x00000800u
#define SCE_CTRL_UP 0x00000010u
#define SCE_CTRL_LEFT 0x00000080u
#define SCE_CTRL_RIGHT 0x00000020u
#define SCE_CTRL_CIRCLE 0x00002000u
#define SCE_CTRL_INTERCEPTED 0x00010000u
#define SCE_CTRL_HEADPHONE 0x00080000u
#define SCE_CTRL_MODE_ANALOG_WIDE 2
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW 1
#define SCE_DISPLAY_PIXELFORMAT_A8B8G8R8 0
#define SCE_DISPLAY_SETBUF_NEXTFRAME 1
#define SCE_KERNEL_POWER_TICK_DEFAULT 0
int sceCtrlPeekBufferPositive(int,SceCtrlData*,int);
int sceCtrlPeekBufferPositiveExt2(int,SceCtrlData*,int);
int sceCtrlSetSamplingMode(int);
int sceKernelDelayThread(unsigned);
int sceKernelPowerTick(int);
int sceDisplayWaitVblankStart(void);
int sceDisplayWaitSetFrameBuf(void);
int sceDisplaySetFrameBuf(const SceDisplayFrameBuf*,int);
int sceKernelAllocMemBlock(const char*,int,unsigned,void*);
int sceKernelGetMemBlockBase(int,void**);
int sceKernelFreeMemBlock(int);
void sceKernelExitProcess(int);
#endif
'''
(w/'menu_stubs.h').write_text(stub)
boot=(src/'utils/boot_check.c').read_text();paths=boot[boot.index('static const char *obb_path'):boot.index('#if !PVZ2_SKIP_BOOT_CHECKS')]
code=r'''
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <csetjmp>
#include <string>
#include <vector>
#include <cerrno>
#include <unistd.h>
#ifdef _WIN32
#include <io.h>
#define fsync _commit
#endif
#include "menu_stubs.h"
#define DATA_PATH "./userdata/"
#define GAME_DATA_PATH "./"
static unsigned reads,frames,delay,exit_code;
static unsigned settings_calls;
static int mode;static jmp_buf exited;static unsigned char *memory;
static const void *visible,*pending;
static std::vector<unsigned char> visible_copy;
static std::vector<unsigned char> source_save;
static bool file_exists(const char *p){FILE*f=fopen(p,"rb");if(!f)return false;fclose(f);return true;}
static std::vector<unsigned char> read_file(const char *path){FILE*f=fopen(path,"rb");if(!f)return {};fseek(f,0,SEEK_END);std::vector<unsigned char>b(ftell(f));rewind(f);fread(b.data(),1,b.size(),f);fclose(f);return b;}
static std::vector<unsigned char> read_save(){return read_file("userdata/No_Backup/pp.dat");}
static std::vector<unsigned> extra_input;
static void press(unsigned button){extra_input.push_back(0);extra_input.push_back(button);}
static void down(unsigned times){while(times--)press(SCE_CTRL_DOWN);}
static void prepare_input(){
 const unsigned chord=SCE_CTRL_DOWN|SCE_CTRL_CROSS|SCE_CTRL_L1|SCE_CTRL_R1;
 extra_input={0,chord,0,0,0};
 if(mode==14||mode==15){
  // Grey language row and unavailable currency/player rows ignore presses.
  down(1);press(SCE_CTRL_RIGHT);down(1);press(SCE_CTRL_CROSS);
  down(1);press(SCE_CTRL_CROSS);down(1);press(SCE_CTRL_CROSS);
  down(1);press(SCE_CTRL_CROSS);
 }else if(mode==16||mode==20){
  down(3);press(SCE_CTRL_CROSS);press(SCE_CTRL_UP);press(SCE_CTRL_CROSS);
  press(SCE_CTRL_UP);press(SCE_CTRL_RIGHT);down(2);
  press(SCE_CTRL_CROSS);press(SCE_CTRL_UP);press(SCE_CTRL_CROSS);
  if(mode==20)press(SCE_CTRL_CIRCLE);
  else {press(SCE_CTRL_UP);press(SCE_CTRL_UP);press(SCE_CTRL_LEFT);down(3);press(SCE_CTRL_CROSS);}
 }else if(mode==17){
  down(3);press(SCE_CTRL_CROSS);press(SCE_CTRL_UP);press(SCE_CTRL_CIRCLE);
  down(2);press(SCE_CTRL_CROSS);
 }else if(mode==18||mode==19){
  down(3);press(SCE_CTRL_CROSS);press(SCE_CTRL_UP);press(SCE_CTRL_CROSS);
  if(mode==19){press(SCE_CTRL_CROSS);press(SCE_CTRL_DOWN);press(SCE_CTRL_CROSS);}
  down(2);press(SCE_CTRL_CROSS);
  if(mode==18)press(SCE_CTRL_CIRCLE);
 }else if(mode==23){
  down(2);press(SCE_CTRL_LEFT);down(1);press(SCE_CTRL_CROSS);
  press(SCE_CTRL_UP);press(SCE_CTRL_CROSS);down(2);press(SCE_CTRL_CROSS);
 }else if(mode==25){
  down(1);press(SCE_CTRL_RIGHT);down(4);press(SCE_CTRL_CROSS);
 }else{down(5);press(SCE_CTRL_CROSS);}
}
extern "C" {
'''+paths+r'''
#include "utils/port_locale.c"
int menu_settings_save(int override,int language){
 ++settings_calls;if(mode==13&&settings_calls==1)return 0;
 return pvz2_port_settings_save(override,language);
}
#define pvz2_port_settings_save menu_settings_save
#include "utils/port_menu.c"
#undef pvz2_port_settings_save
}
static int menu_editor_sync(int fd){
 if(mode==18){errno=ENOSPC;return -1;}
 return fsync(fd);
}
#undef fsync
#define fsync menu_editor_sync
#include "utils/save_editor.cpp"
#undef fsync
int sceCtrlSetSamplingMode(int mode){return 0;}
int sceCtrlPeekBufferPositive(int port,SceCtrlData *p,int n){
 // Legacy physical L/R bits must not satisfy an L1/R1 chord. The old
 // production code called this API and failed to open on real hardware.
 p->buttons=SCE_CTRL_DOWN|SCE_CTRL_CROSS|SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER;
 return 1;
}
int sceCtrlPeekBufferPositiveExt2(int port,SceCtrlData *p,int n){
 struct Status{SceCtrlData*p;~Status(){if(mode==7)p->buttons|=SCE_CTRL_HEADPHONE;}}status{p};
 ++reads;if(mode==0){p->buttons=SCE_CTRL_DOWN|SCE_CTRL_CROSS|SCE_CTRL_L1;return 1;}
 if(mode==3&&reads<90){p->buttons=0;return 1;}
 unsigned step=mode==3?reads-89:reads;
 if(mode>=14){
  assert(step<extra_input.size());p->buttons=extra_input[step];
  // Only Done may touch disk; failed-save mode never changes preferences.
  if(step<extra_input.size()-1 || mode==18){
   assert(read_save()==source_save);
   if(mode!=24&&mode!=25)assert(!file_exists("userdata/port_settings.txt"));
  }
  return 1;
 }
 const unsigned chord=SCE_CTRL_DOWN|SCE_CTRL_CROSS|SCE_CTRL_L1|SCE_CTRL_R1;
 if(step==1){p->buttons=mode==4?0:chord;return 1;}
 if(mode==5){
  const unsigned held[]={0,chord,chord,chord&~SCE_CTRL_CROSS,chord,SCE_CTRL_INTERCEPTED,chord,0,chord,SCE_CTRL_DOWN,SCE_CTRL_CROSS,0,0,SCE_CTRL_INTERCEPTED,0,0,0,0,SCE_CTRL_CIRCLE};
  assert(step<sizeof(held)/sizeof(*held));p->buttons=held[step];return 1;
 }
 if(mode==8||mode==9){
  const unsigned resume[]={0,chord,0,0,0,SCE_CTRL_INTERCEPTED,SCE_CTRL_CROSS,0,SCE_CTRL_CROSS,0,0,0,SCE_CTRL_CIRCLE};
  assert(step<sizeof(resume)/sizeof(*resume));p->buttons=resume[step];return mode==9&&step==5?0:1;
 }
 if(mode==6||mode==7){
  assert(step<=15);p->buttons=step==15?SCE_CTRL_CROSS:(step>=5&&step%2)?SCE_CTRL_DOWN:0;return 1;
 }
 if(mode>=2&&mode!=13){p->buttons=step==5?SCE_CTRL_RIGHT:step==7?SCE_CTRL_CIRCLE:0;return 1;}
 if(mode==13&&reads>=32){assert(reads<=33);p->buttons=reads==33?SCE_CTRL_CROSS:0;return 1;}
 // Every staged action before Done must leave both files untouched.
 if(reads<31){assert(read_save()==source_save);assert(!file_exists("userdata/port_settings.txt"));}
 const unsigned sequence[]={0,chord,0,0,0,SCE_CTRL_RIGHT,0,SCE_CTRL_DOWN,0,SCE_CTRL_RIGHT,0,SCE_CTRL_DOWN,0,SCE_CTRL_DOWN,0,SCE_CTRL_CROSS,0,SCE_CTRL_UP,0,SCE_CTRL_CROSS,0,SCE_CTRL_DOWN,0,SCE_CTRL_CROSS,0,SCE_CTRL_UP,0,SCE_CTRL_CROSS,0,SCE_CTRL_DOWN,0,SCE_CTRL_CROSS};
 assert(reads<sizeof(sequence)/sizeof(*sequence));p->buttons=sequence[reads];return 1;
}
int sceKernelDelayThread(unsigned us){++delay;return 0;}
int sceKernelPowerTick(int){return 0;}
int sceDisplayWaitVblankStart(void){return 0;}
int sceKernelAllocMemBlock(const char*,int,unsigned n,void*){if(mode==10)return -1;assert(n==4*1024*1024);memory=(unsigned char*)malloc(n+128);assert(memory);memset(memory,0x5a,n+128);return 1;}
int sceKernelGetMemBlockBase(int,void**p){*p=memory+64;return 0;}
int sceKernelFreeMemBlock(int){free(memory);memory=nullptr;return 0;}
int sceDisplaySetFrameBuf(const SceDisplayFrameBuf *fb,int){
 if(!fb)return 0;++frames;assert(fb->width==960&&fb->height==544);
 assert(!pending&&fb->base!=visible);
 if(visible)assert(!memcmp(visible,visible_copy.data(),visible_copy.size()));
 assert(fb->base==memory+64+((frames-1)%2)*2*1024*1024);
 pending=fb->base;
 for(int i=0;i<64;++i)assert(memory[i]==0x5a&&memory[64+4*1024*1024+i]==0x5a);
 // No launch-button edge or interception may move the row or enable override.
 if((frames==1&&mode!=24) || mode==5 || mode==8 || mode==9){
  std::vector<uint32_t> expected(960*544,0xff251a16);
  text(expected.data(),64,174,"Language override: Off",0xff84e4ac);
  text(expected.data(),64,204,"Language: English (automatic)",0xff999999);
  for(int y: {174,204})for(int line=0;line<16;++line)assert(!memcmp((uint32_t*)fb->base+(y+line)*960+64,expected.data()+(y+line)*960+64,700*4));
 }
 if(mode==21||mode==22){
  std::vector<uint32_t> expected(960*544,0xff251a16);
  text(expected.data(),32,82,mode==21?"Archive is missing or unreadable.":"4 bytes - unrecognized archive header",0xffeeeeee);
  for(int y=82;y<98;++y)assert(!memcmp((uint32_t*)fb->base+y*960+32,expected.data()+y*960+32,896*4));
 }
 if(frames==1||frames==12){char p[64];snprintf(p,sizeof(p),"screen-%u.rgba",frames);FILE*f=fopen(p,"wb");assert(f);fwrite(fb->base,4,960*544,f);fclose(f);}
 if(mode==13&&settings_calls==1){
  assert(!file_exists("userdata/port_settings.txt"));
  std::vector<uint32_t> expected(960*544,0xff251a16);
  text(expected.data(),32,364,"Currencies saved; settings failed. Check free space and retry Done.",0xffeeeeee);
  for(int y=364;y<430;++y)assert(!memcmp((uint32_t*)fb->base+y*960+32,expected.data()+y*960+32,896*4));
 }
 if(mode==18&&reads>=extra_input.size()-3){
  std::vector<uint32_t> expected(960*544,0xff251a16);
  text(expected.data(),32,364,"Backup failed. Original save preserved.",0xffeeeeee);
  for(int y=364;y<430;++y)assert(!memcmp((uint32_t*)fb->base+y*960+32,expected.data()+y*960+32,896*4));
 }
 if(mode==11)return -1;
 return 0;
}
int sceDisplayWaitSetFrameBuf(void){
 if(mode==12)return -1;
 assert(pending);visible=pending;pending=nullptr;
 const unsigned char *p=(const unsigned char*)visible;
 visible_copy.assign(p,p+960*544*4);return 0;
}
void sceKernelExitProcess(int code){exit_code=code;longjmp(exited,1);}
extern "C" void pvz2_boot_screen(const char*){exit_code=9;longjmp(exited,1);}
int main(int argc,char **argv){
 assert(argc==2);mode=atoi(argv[1]);source_save=mode==26?read_file("userdata/No_Backup/pp.dat.editor-old"):read_save();
 if(mode>=14)prepare_input();pvz2_locale_load();
 if(mode==24)assert(pvz2_locale_override()&&!strcmp(pvz2_locale(),"de_DE"));
 else assert(!pvz2_locale_override()&&!strcmp(pvz2_locale(),"en_US"));
 if(!setjmp(exited)){pvz2_port_menu();assert(mode==0);}
 if(mode==0){assert(reads==90&&delay==90&&!frames);assert(read_save()==source_save);}
 if(mode>=2&&mode<=9){assert(exit_code==0&&frames);assert(read_save()==source_save);}
 if(mode>=2&&mode<=9&&mode!=6&&mode!=7)assert(!file_exists("userdata/port_settings.txt"));
 if(mode>=10&&mode<=12){assert(exit_code==9&&read_save()==source_save);assert(!file_exists("userdata/port_settings.txt"));}
 if(mode==6||mode==7){pvz2_locale_load();assert(!pvz2_locale_override()&&pvz2_locale_index()==0&&!strcmp(pvz2_locale(),"en_US"));FILE*f=fopen("userdata/port_settings.txt","rb");assert(f);int v,o,l;assert(fscanf(f,"%d %d %d",&v,&o,&l)==3&&v==3&&o==0&&l==0);fclose(f);assert(!file_exists("userdata/No_Backup/pp.dat.backup-0"));}
 if(mode==3)assert(delay==89&&reads>90);
 if(mode==4)assert(delay==0);
 if(mode==1||mode==13){
  assert(exit_code==0&&frames>20);pvz2_locale_load();assert(pvz2_locale_index()==1&&pvz2_locale_override());
  assert(!strcmp(pvz2_locale(),"de_DE")&&!strcmp(pvz2_language(),"de")&&!strcmp(pvz2_country(),"DE"));
  Pvz2SaveProfile p[16];char error[256];assert(pvz2_save_read("userdata/No_Backup/pp.dat",p,16,error,sizeof(error))==1&&p[0].coins==1&&p[0].gems==1);
  assert(file_exists("userdata/No_Backup/pp.dat.backup-0"));
  assert(!file_exists("userdata/No_Backup/pp.dat.backup-1"));
  if(mode==13)assert(settings_calls==2);
  for(int i=0;i<6;++i){FILE*f=fopen("userdata/port_settings.txt","wb");assert(f);fprintf(f,"2 1 %d\n",i);fclose(f);pvz2_locale_load();assert(!pvz2_locale_override()&&pvz2_locale_index()==0&&!strcmp(pvz2_locale(),"en_US"));}
  for(int i=0;i<6;++i){assert(pvz2_port_settings_save(1,i));pvz2_locale_load();assert(pvz2_locale_index()==i&&pvz2_locale_override());}
  assert(pvz2_port_settings_save(0,1));pvz2_locale_load();
  assert(!pvz2_locale_override()&&!strcmp(pvz2_locale(),"en_US")&&!strcmp(pvz2_language(),"en")&&!strcmp(pvz2_country(),"US"));
  assert(!rename("userdata/port_settings.txt","userdata/port_settings.txt.old"));pvz2_locale_load();assert(!pvz2_locale_override());

 }
 if(mode>=14){
  assert(exit_code==0&&frames&&reads==extra_input.size()-1);
  if(mode!=16&&mode!=23)assert(read_save()==source_save);
  if(mode==14)assert(!file_exists("userdata/No_Backup/pp.dat"));
  if(mode==18||mode==20){assert(!settings_calls&&!file_exists("userdata/port_settings.txt"));}
  else {
   assert(settings_calls==1);pvz2_locale_load();
   assert(pvz2_locale_override()==(mode==24));
   assert(pvz2_locale_index()==((mode==24||mode==25)?1:0));
   assert(!strcmp(pvz2_locale(),mode==24?"de_DE":"en_US"));
  }
  if(mode==16||mode==23){
   assert(read_file("userdata/No_Backup/pp.dat.backup-0")==source_save);
   assert(!file_exists("userdata/No_Backup/pp.dat.backup-1"));
   Pvz2SaveProfile p[16];char error[256];int count=pvz2_save_read("userdata/No_Backup/pp.dat",p,16,error,sizeof(error));
   if(mode==16)assert(count==2&&p[0].coins==1&&p[0].gems==3&&p[1].coins==234&&p[1].gems==457);
   else {assert(count==16);for(int i=0;i<16;++i)assert(p[i].coins==unsigned(i==15?16:i)&&p[i].gems==unsigned(i+100));}
  }else if(mode!=18)assert(!file_exists("userdata/No_Backup/pp.dat.backup-0"));
  if(mode==26)assert(!file_exists("userdata/No_Backup/pp.dat.editor-old"));
  printf("PASS: update scenario %d; disk state and preference commit verified\n",mode);
 }
 if(memory)free(memory);
 if(mode<14)printf("PASS: boot-menu regression %d; input/render/disk assertions verified\n",mode);
}
'''
(w/'test.cpp').write_text(code);exe=w/'test.exe'
subprocess.run(['g++','-O2','-static','-std=c++17','-I',str(w),'-I',str(src),str(w/'test.cpp'),'-o',str(exe)],check=True,timeout=30)
def st(x):return b'\x81'+bytes([len(x.encode())])+x.encode()
def num(n):
 import struct
 return b'\x20'+struct.pack('<i',n)
def obj(items):return b'\x85'+b''.join(st(k)+v for k,v in items)+b'\xff'
def profile(name,c,g):return obj([('objclass',st('PlayerInfo')),('objdata',obj([('n',st(name)),('c',num(c)),('g',num(g))]))])
def document(players):return b'RTON\x01\0\0\0'+st('objects')+b'\x86\xfd'+bytes([len(players)])+b''.join(players)+b'\xfe\xffDONE'
scenario_names={14:'missing save',15:'unsupported save',16:'two-player edits commit once',17:'cancel high-balance edit',18:'save sync failure blocks preference commit',19:'undo staged edit leaves save untouched',20:'cancel all multi-player edits',21:'missing OBB still allows settings',22:'unknown OBB header displayed',23:'16-player left wrap edits only last profile',24:'explicit v3 override survives Done',25:'v3 Off stays English with alternate stored locale',26:'interrupted-save recovery before menu'}
for mode in [0,2,3,4,5,6,7,8,9,10,11,12,1,13,*scenario_names]:
 d=w/str(mode);(d/'userdata/No_Backup').mkdir(parents=True)
 if mode==4:(d/'userdata/port_menu.txt').write_text('')
 if mode==6:(d/'userdata/port_settings.txt').write_text('2 1 1\n')
 if mode in (24,25):(d/'userdata/port_settings.txt').write_text(f'3 {int(mode==24)} 1\n')
 example=r/'out/save-check-20260905-210533/No_Backup/pp.dat'
 if example.exists():shutil.copy2(example,d/'userdata/No_Backup/pp.dat')
 else:
  data=b'RTON\x01\0\0\0'+st('objects')+b'\x86\xfd\x01\x85'+st('objclass')+st('PlayerInfo')+st('objdata')+b'\x85'+st('n')+st('Test')+st('c')+b'\x21'+st('g')+b'\x21\xff\xff\xfe\xffDONE'
  (d/'userdata/No_Backup/pp.dat').write_bytes(data)
 save=d/'userdata/No_Backup/pp.dat'
 if mode==14:save.unlink()
 elif mode==15:save.write_bytes(b'\x10encrypted or unsupported save')
 elif mode in (16,20):save.write_bytes(document([profile('One',0,3),profile('Two',234,456)]))
 elif mode==17:save.write_bytes(document([profile('Rich',2000000000,1500000000)]))
 elif mode==23:save.write_bytes(document([profile(f'Player {i}\n',i,i+100) for i in range(16)]))
 elif mode==26:save.rename(save.with_name('pp.dat.editor-old'))
 # Read only the RSB header; never copy or modify proprietary archive data.
 archive=r/'game/main.147.com.ea.game.pvz2_row.obb'
 if mode==21:pass
 elif mode==22:(d/'game.obb').write_bytes(b'bad!')
 elif archive.exists():os.link(archive,d/'game.obb')
 else:
  import struct
  data=bytearray(256);data[:8]=b'1bsr\x04\0\0\0';struct.pack_into('<I',data,40,3)
  (d/'game.obb').write_bytes(data)
 try:
  run=subprocess.run([str(exe),str(mode)],cwd=d,check=True,capture_output=True,text=True,timeout=10)
  if mode in scenario_names:print(scenario_names[mode])
  print(run.stdout.strip())
  if mode==16:assert save.read_bytes()==document([profile('One',1,3),profile('Two',234,457)])
  if mode==23:assert save.read_bytes()==document([profile(f'Player {i}\n',i+int(i==15),i+100) for i in range(16)])
 finally:(d/'game.obb').unlink(missing_ok=True)
for p in w.rglob('*.rgba'):
 Image.frombytes('RGBA',(960,544),p.read_bytes()).save(p.with_suffix('.png'))
print(w)
