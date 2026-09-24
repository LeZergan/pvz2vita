"""Exercise production controller/settings without a console or emulator."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1];s=r/'vita/direct/source'
w=Path(tempfile.mkdtemp(prefix='controller-',dir=r/'out'))
for name in ['ctrl.h','kernel/processmgr.h','message_dialog.h']:
    p=w/'psp2'/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('')
code=r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define DATA_PATH ""
#define SCE_CTRL_UP 1
#define SCE_CTRL_DOWN 2
#define SCE_CTRL_LEFT 4
#define SCE_CTRL_RIGHT 8
#define SCE_CTRL_CROSS 16
#define SCE_CTRL_CIRCLE 32
#define SCE_CTRL_L1 64
#define SCE_CTRL_R1 128
#define SCE_CTRL_START 256
#define SCE_MSG_DIALOG_MODE_USER_MSG 1
#define SCE_MSG_DIALOG_BUTTON_TYPE_OK 0
#define SCE_COMMON_DIALOG_STATUS_FINISHED 2
typedef struct {const char *msg1,*msg2,*msg3;} SceMsgDialogButtonsParam;
typedef struct {const char *msg;int buttonType;SceMsgDialogButtonsParam *buttonParam;} SceMsgDialogUserMessageParam;
typedef struct {int mode;SceMsgDialogUserMessageParam *userMsgParam;} SceMsgDialogParam;
typedef struct {int result,buttonId;} SceMsgDialogResult;
static uint64_t now;
static unsigned fps=30,events,downs,ups,keys,dialogs;
static int active,dialog_status,dialog_rc,button_id;
static int keyboard_showing;
int pvz2_keyboard_is_showing(void){return keyboard_showing;}
static unsigned held_keys;
uint64_t sceKernelGetSystemTimeWide(void){return now;}
void sceMsgDialogParamInit(SceMsgDialogParam*p){memset(p,0,sizeof(*p));}
int sceMsgDialogInit(const SceMsgDialogParam*p){assert(p->userMsgParam->buttonType==SCE_MSG_DIALOG_BUTTON_TYPE_OK && strstr(p->userMsgParam->msg,"Fixed 30 FPS"));++dialogs;return dialog_rc;}
int sceMsgDialogGetStatus(void){return dialog_status;}
int sceMsgDialogGetResult(SceMsgDialogResult*p){p->buttonId=button_id;return 0;}
int sceMsgDialogTerm(void){dialog_status=0;return 0;}
void telemetry_log(const char*a,const char*b,...){(void)a;(void)b;}
void pvz2_present_set_fps(unsigned f){fps=f;}
unsigned pvz2_present_budget_us(void){return (1000000+fps-1)/fps;}
#include "utils/controller.c"
void controls_release_for_dialog(void){pvz2_controller_release();}
void controls_restore_sampling(void){}
void pvz2_input_push_key(int code,int down){unsigned bit=code==82?1:2;assert(code==82||code==4);if(down){assert(!(held_keys&bit));held_keys|=bit;}else{assert(held_keys&bit);held_keys&=~bit;}++keys;}
void pvz2_input_push_touch(int id,int phase,int x,int y){
 assert(id==254 && x>=0 && x<=959 && y>=0 && y<=543);++events;
 if(phase==PVZ2_INPUT_TOUCH_DOWN){assert(!active);active=1;++downs;}
 else if(phase==PVZ2_INPUT_TOUCH_UP){assert(active);active=0;++ups;}
 else assert(active);
}
static void pad(unsigned b){now+=16667;pvz2_controller_pad(b,128,128,128,128);}
int main(void){
 pad(0);pad(SCE_CTRL_CROSS);assert(active && downs==1);
 for(int i=0;i<60;i++)pad(SCE_CTRL_CROSS|SCE_CTRL_RIGHT);
 pad(0);assert(!active && ups==1 && cursor_x>790);
 float old=cursor_x;now+=10000000;pvz2_controller_pad(SCE_CTRL_RIGHT,255,128,128,128);
 assert(cursor_x-old<=49);pad(0);
 for(int i=0;i<300;i++)pad(SCE_CTRL_LEFT|SCE_CTRL_UP);
 assert(cursor_x==0 && cursor_y==0);pad(0);
 /* Tray navigation requires explicit confirmation, then returns to lawn. */
 unsigned before=events;pad(SCE_CTRL_R1);pad(0);assert(events==before && cursor_x==45 && cursor_y==86);
 pad(SCE_CTRL_R1);pad(0);assert(cursor_y==140);
 pad(SCE_CTRL_CROSS);pad(0);assert(cursor_x==0 && cursor_y==0 && !active);
 /* Re-enter the previously selected slot; Back cancels tray focus only. */
 pad(SCE_CTRL_R1);pad(0);assert(cursor_y==140);
 before=keys;pad(SCE_CTRL_CIRCLE);pad(0);assert(keys==before && cursor_x==0 && cursor_y==0);
 /* Physical touch and reserved shoulder combinations release game keys. */
 pad(SCE_CTRL_START|SCE_CTRL_CIRCLE);assert(held_keys==3);
 pvz2_controller_touch(2,1,0,0);assert(!held_keys);
 pvz2_controller_touch(2,0,0,0);pad(0);
 pad(SCE_CTRL_START);pad(SCE_CTRL_L1|SCE_CTRL_R1);assert(!held_keys);pad(0);
 pad(SCE_CTRL_CIRCLE);pad(SETTINGS_CHORD);assert(!held_keys);
 visual_requested=0;pad(0);
 /* Movement resumes after touch without centering either stick. */
 pvz2_controller_touch(1,1,400,240);pvz2_controller_touch(1,0,400,240);
 for(int i=0;i<5;i++){now+=16667;pvz2_controller_pad(0,255,128,128,128);}
 assert(cursor_x>400);pad(0);
 before=events;pad(SETTINGS_CHORD);assert(events==before && visual_requested);
 assert(pvz2_visual_poll() && dialogs==1 && pvz2_visual_active());
 for(int i=0;i<60;i++)pad(SETTINGS_CHORD);
 assert(dialogs==1 && events==before);dialog_status=2;button_id=2;
 assert(pvz2_visual_poll() && fps==30 && !pvz2_visual_active());
 pad(SCE_CTRL_CROSS);assert(events==before);pad(0);pad(SCE_CTRL_CROSS);assert(active);pad(0);
 /* Physical touch takes ownership; controller cannot create a second touch. */
 pad(SCE_CTRL_CROSS);pvz2_controller_touch(3,1,220,180);assert(!active);
 before=events;pad(0);pad(SCE_CTRL_CROSS);assert(events==before);
 pvz2_controller_touch(3,0,220,180);pad(0);pad(SCE_CTRL_CROSS);assert(active);
 pvz2_controller_release();assert(!active);pad(0);
 /* Failed dialog initialization never strands input. */
 dialog_rc=-1;pad(SETTINGS_CHORD);assert(pvz2_visual_poll());assert(!pvz2_visual_active());
 pad(0);before=downs;pad(SCE_CTRL_CROSS);pad(0);assert(downs==before+1);
 assert(downs==ups && !active);
 int x,y;assert(pvz2_controller_cursor(&x,&y));
 keyboard_showing=1;assert(!pvz2_controller_cursor(&x,&y));
 pvz2_controller_release();assert(!pvz2_controller_cursor(&x,&y));
 keyboard_showing=0;assert(pvz2_controller_cursor(&x,&y));pad(0);
 for(int i=0;i<181;i++)pad(0);
 assert(pvz2_controller_cursor(&x,&y));
 for(int cycle=0;cycle<500;cycle++){
  pvz2_controller_touch(1,1,400,240);
  now+=33334;pvz2_controller_pad(SCE_CTRL_CROSS,255,128,128,128);
  assert(!active && cursor_x==400);
  pvz2_controller_touch(1,0,400,240);
  unsigned count=downs;
  now+=33334;pvz2_controller_pad(SCE_CTRL_CROSS,255,128,128,128);
  assert(cursor_x>400 && !active && downs==count);
  now+=33334;pvz2_controller_pad(0,255,128,128,128);
  now+=33334;pvz2_controller_pad(SCE_CTRL_CROSS,255,128,128,128);
  assert(active && downs==count+1);pad(0);assert(!active && downs==ups);
 }
 pvz2_controller_touch(1,1,-50,900);pvz2_controller_touch(1,0,-50,900);
 assert(cursor_x==0 && cursor_y==543);pad(0);
 puts("PASS: pointer/drag, clamping, hitch bound, seed focus/return, chord, 30 FPS, modal release, touch ownership, init failure");
}
'''
(w/'check.c').write_text(code)
subprocess.run(['gcc','-std=gnu11','-O2','-I'+str(w),'-I'+str(s),str(w/'check.c'),'-o',str(w/'check.exe')],check=True)
subprocess.run([str(w/'check.exe')],check=True,timeout=5,cwd=w)
gl=(s/'utils/glutil.c').read_text(encoding='utf-8')
start=gl.index('static void controller_cursor_rect(');end=gl.index('\n}',gl.index('static void controller_draw_cursor(void)'))+2
cursor=r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef int GLint;typedef float GLfloat;typedef unsigned char GLboolean;
enum {GL_SCISSOR_TEST,GL_FRAMEBUFFER_BINDING,GL_SCISSOR_BOX,GL_COLOR_CLEAR_VALUE,GL_COLOR_WRITEMASK,GL_FRAMEBUFFER,GL_COLOR_BUFFER_BIT};
#define GL_TRUE 1
typedef struct {int fbo,box[4];float color[4];GLboolean mask[4],scissor;} State;
static State st;static unsigned draws,calls;static int shown,cx=220,cy=180;static unsigned char raster[544][960];
int pvz2_controller_cursor(int*x,int*y){*x=cx;*y=cy;return shown;}
int glIsEnabled(int e){(void)e;++calls;return st.scissor;}
void glGetIntegerv(int e,int*p){++calls;if(e==GL_FRAMEBUFFER_BINDING)*p=st.fbo;else memcpy(p,st.box,sizeof(st.box));}
void glGetFloatv(int e,float*p){(void)e;memcpy(p,st.color,sizeof(st.color));}
void glGetBooleanv(int e,GLboolean*p){(void)e;memcpy(p,st.mask,sizeof(st.mask));}
void glBindFramebuffer(int e,int f){(void)e;st.fbo=f;}
void glEnable(int e){(void)e;st.scissor=1;}void glDisable(int e){(void)e;st.scissor=0;}
void glColorMask(int a,int b,int c,int d){st.mask[0]=a;st.mask[1]=b;st.mask[2]=c;st.mask[3]=d;}
void glClearColor(float a,float b,float c,float d){st.color[0]=a;st.color[1]=b;st.color[2]=c;st.color[3]=d;}
void glScissor(int a,int b,int c,int d){st.box[0]=a;st.box[1]=b;st.box[2]=c;st.box[3]=d;}
void glClear(int e){(void)e;assert(st.fbo==0 && st.scissor && st.box[2]<=19 && st.box[3]<=19);assert(st.box[0]>=0 && st.box[1]>=0 && st.box[2]>0 && st.box[3]>0);
 assert(st.box[0]+st.box[2]<=960 && st.box[1]+st.box[3]<=544);
 for(int y=st.box[1];y<st.box[1]+st.box[3];y++)for(int x=st.box[0];x<st.box[0]+st.box[2];x++)raster[y][x]=1;
 ++draws;}
'''+gl[start:end]+r'''
int main(void){
 controller_draw_cursor();assert(!calls && !draws);
 for(int i=0;i<2;i++){
  st=(State){.fbo=17,.box={33,55,720,344},.color={.1f,.2f,.3f,.4f},.mask={1,0,1,0},.scissor=i};
  State old=st;shown=1;controller_draw_cursor();assert(!memcmp(&old,&st,sizeof(st)));
 }
 assert(draws==8);
 /* Every pixel along each edge plus corners: no elongated strip, spill or
  * leftover clip/FBO/color state. Compare against the clipped 19px cross. */
 for(int edge=0;edge<4;edge++)for(int at=0;at<(edge<2?960:544);at++){
  cx=edge<2?at:(edge==2?0:959);cy=edge<2?(edge==0?0:543):at;
  memset(raster,0,sizeof(raster));State old=st;controller_draw_cursor();assert(!memcmp(&old,&st,sizeof(st)));
  for(int y=0;y<544;y++)for(int x=0;x<960;x++){
   int dx=x-cx,dy=y-(543-cy);if(dx<0)dx=-dx;if(dy<0)dy=-dy;
   assert(raster[y][x]==((dx<=9 && dy<=3)||(dx<=3 && dy<=9)));
  }
 }
 puts("PASS: all 3008 edge positions rasterize the exact clipped cross without spill; framebuffer/scissor/color/masks restored; hidden pointer zero GL calls");
}
'''
(w/'cursor.c').write_text(cursor)
subprocess.run(['gcc','-std=c11','-O2',str(w/'cursor.c'),'-o',str(w/'cursor.exe')],check=True)
subprocess.run([str(w/'cursor.exe')],check=True,timeout=15)
start=gl.index('unsigned pvz2_present_budget_us(void)');end=gl.index('\n/* Clip before',start)
pacing='#include <assert.h>\n#include "utils/fps_preference.h"\n'+gl[start:end]+'\nint main(void){assert(pvz2_present_budget_us()==33334);}'
(w/'pacing.c').write_text(pacing)
subprocess.run(['gcc','-std=c11','-O2','-I'+str(s),str(w/'pacing.c'),'-o',str(w/'pacing.exe')],check=True)
subprocess.run([str(w/'pacing.exe')],check=True,timeout=5)
assert 'pvz2_present_set_fps' not in gl and 'fps_load' not in gl and 'novsync.txt' not in gl
print('PASS: fixed 30 FPS from startup; no preference setter or vsync override')
print(w)
