"""Production touch/gesture and sprite matrix checks; no emulator or UI."""
from pathlib import Path
import subprocess, tempfile

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT/'vita/direct/source'
WORK = Path(tempfile.mkdtemp(prefix='touch-render-', dir=ROOT/'out'))
for name in ['ctrl.h','touch.h','motion.h','kernel/clib.h','kernel/processmgr.h']:
    p=WORK/'psp2'/name
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text('/* SDK calls supplied by the test adapter. */\n')

def execute(name, code):
    path=WORK/(name+'.c'); path.write_text(code, encoding='utf-8')
    exe=path.with_suffix('.exe')
    subprocess.run(['gcc','-std=gnu11','-O2','-static','-pthread','-I'+str(WORK),
                    '-I'+str(SRC),str(path),'-lm','-o',str(exe)],check=True,timeout=30)
    return subprocess.run([str(exe)],check=True,capture_output=True,text=True,timeout=5).stdout

java=(SRC/'java.c').read_text(encoding='utf-8')
packet=java[java.index('#define PVZ2_INPUT_QUEUE_CAP'):java.index('static jboolean UIProcessEvents(')]
touch=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>
#include "java_runtime.h"
#define l_info(...) ((void)0)
void pvz2_keyboard_text_delivered(void) {}
'''+packet+r'''
#include "reimpl/controls.h"
#define SCE_TOUCH_MAX_REPORT 8
#define SCE_TOUCH_PORT_FRONT 0
#define SCE_TOUCH_SAMPLING_STATE_START 1
#define SCE_CTRL_MODE_ANALOG_WIDE 1
#define SCE_KERNEL_POWER_TICK_DEFAULT 0
typedef int SceTouchSamplingState;
typedef struct {uint8_t id; int16_t x,y;} SceTouchReport;
typedef struct {uint64_t timeStamp; unsigned status, reportNum; SceTouchReport report[8];} SceTouchData;
typedef struct {unsigned buttons; uint8_t lx,ly,rx,ry;} SceCtrlData;
static SceTouchData sample;
static SceCtrlData pad_sample={.lx=128,.ly=128,.rx=128,.ry=128};
static int read_rc=1,pad_rc=1,sampling=1,power_ticks,restart_calls;
static int sceTouchPeek(int p,SceTouchData *t,int n) {*t=sample;return read_rc;}
static int sceCtrlPeekBufferPositiveExt2(int p,SceCtrlData *t,int n) {*t=pad_sample;return pad_rc;}
static int sceTouchGetSamplingState(int p,int *s) {*s=sampling;return 0;}
static int sceTouchSetSamplingState(int p,int s) {sampling=s;++restart_calls;return 0;}
static void sceCtrlSetSamplingModeExt(int s) {}
static void sceMotionStartSampling(void) {}
static void sceKernelPowerTick(int s) {++power_ticks;}
#define sceClibMemcpy memcpy
void controls_handler_touch(int32_t id,float x,float y,ControlsAction a) {
 pvz2_input_push_touch(id,a==CONTROLS_ACTION_DOWN?PVZ2_INPUT_TOUCH_DOWN:
   a==CONTROLS_ACTION_UP?PVZ2_INPUT_TOUCH_UP:PVZ2_INPUT_TOUCH_MOVE,(int)x,(int)y);
}
void controls_handler_key(int32_t k,ControlsAction a) {pvz2_input_push_key(k,a==CONTROLS_ACTION_DOWN);}
void controls_handler_analog(ControlsStickId w,float x,float y,ControlsAction a) {}
'''
for i,name in enumerate(['UP','DOWN','LEFT','RIGHT','CROSS','CIRCLE','SQUARE','TRIANGLE','L1','R1','START','SELECT']):
    touch+=f'#define SCE_CTRL_{name} (1u<<{i})\n'
touch+=r'''
#include "reimpl/controls.c"
static unsigned char wire[4096];
static uint32_t u32(unsigned n) {uint32_t v;memcpy(&v,wire+n,4);return v;}
static unsigned drain(void) {return input_drain_to_buffer(wire,sizeof(wire));}
int main(void) {
 controls_init();
 sample.reportNum=2;sample.report[0]=(SceTouchReport){.id=3,.x=100,.y=200};
 sample.report[1]=(SceTouchReport){.id=7,.x=300,.y=400};controls_poll();assert(drain()==2);
 uint32_t a=u32(20),b=u32(68);assert(a && b && a!=b);
 sample.reportNum=1;sample.report[0]=sample.report[1];controls_poll();assert(drain()==1);
 assert(u32(20)==a && u32(52)==PVZ2_INPUT_TOUCH_UP);
 sample.reportNum=0;controls_poll();assert(drain()==1 && u32(20)==b && u32(52)==PVZ2_INPUT_TOUCH_UP);
 /* Replace one finger with another between polls: UP precedes new DOWN. */
 sample.reportNum=1;sample.report[0].id=9;controls_poll();assert(drain()==1);a=u32(20);
 sample.report[0].id=10;controls_poll();assert(drain()==2);
 assert(u32(20)==a && u32(52)==PVZ2_INPUT_TOUCH_UP && u32(100)==PVZ2_INPUT_TOUCH_DOWN);
 b=u32(68);assert(a!=b);
 /* No-sample reads don't fabricate releases or moves. Errors release once. */
 read_rc=0;controls_poll();assert(!drain());
 read_rc=-123;controls_poll();assert(drain()==1 && u32(20)==b && u32(52)==PVZ2_INPUT_TOUCH_UP);
 controls_poll();assert(!drain());controls_tick(1000000);assert(touch_restarts==1);
 read_rc=1;controls_poll();assert(drain()==1 && u32(20)!=b);b=u32(20);
 /* IME closing contact is held outside game until a VALID neutral sample. */
 controls_release_for_dialog();assert(drain()==1 && u32(20)==b);
 controls_poll();assert(!drain() && dialog_release_barrier);
 sample.reportNum=0;read_rc=-1;controls_poll();assert(dialog_release_barrier);
 read_rc=1;pad_sample.buttons=SCE_CTRL_CROSS;controls_poll();assert(dialog_release_barrier);
 pad_sample.buttons=0;controls_poll();assert(!dialog_release_barrier && !drain());
 /* Long idle then a system-disabled panel: sampling restarts and next tap works. */
 for(uint64_t t=2000000;t<22000000;t+=16667) {controls_tick(t);controls_poll();assert(!drain());}
 assert(power_ticks>=20 && power_ticks<=22);
 sampling=0;controls_tick(23000000);assert(sampling==1 && touch_restarts==2);
 sample.reportNum=1;sample.report[0].id=255;controls_poll();assert(drain()==1 && u32(52)==PVZ2_INPUT_TOUCH_DOWN);
 sample.reportNum=9;controls_poll();assert(drain()==1 && !touch_old.reportNum);
 sample.reportNum=0;controls_poll();assert(!drain());
 /* Byte IDs can be reused; game handles stay nonzero, including integer wrap. */
 g_next_touch_id=UINT32_MAX;pvz2_input_push_touch(1,PVZ2_INPUT_TOUCH_DOWN,1,1);
 pvz2_input_push_touch(2,PVZ2_INPUT_TOUCH_DOWN,2,2);assert(drain()==2 && u32(20)==UINT32_MAX && u32(68)==1);
 pvz2_input_push_touch(1,PVZ2_INPUT_TOUCH_UP,1,1);pvz2_input_push_touch(2,PVZ2_INPUT_TOUCH_UP,2,2);assert(drain()==2);
 assert(u32(20)==UINT32_MAX && u32(68)==1);
 pvz2_input_push_touch(-1,0,0,0);pvz2_input_push_touch(256,0,0,0);assert(!drain());
 puts("PASS: overlapping contacts retain separate matching IDs; replacement releases first; no-sample/error recovery; IME release barrier; 20s idle/restart/new tap; malformed samples; ID reuse/wrap");
}
'''

gl=(SRC/'utils/glutil.c').read_text(encoding='utf-8')
def part(a,b):return gl[gl.index(a):gl.index(b)]
render=r'''
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
typedef unsigned GLuint,GLenum;
typedef int GLint,GLsizei,GLboolean;
typedef float GLfloat;
#define GL_FALSE 0
#define GL_ACTIVE_UNIFORMS 1
#define GL_FLOAT_MAT4 2
#define GL_CURRENT_PROGRAM 3
#define l_warn(...) ((void)0)
static int current=1,uniforms=1,comp,driver_calls;
static unsigned pvz2_matrix_uploads_skipped;
static float driver_value[16];
static int g_last_mat4_loc,g_last_mat4_have;static float g_last_mat4[16];
static void glGetIntegerv(int q,int *v) {*v=current;}
static void glGetProgramiv(unsigned p,int q,int *v) {*v=uniforms;}
static void glGetActiveUniform(unsigned p,unsigned i,int cap,int *len,int *size,unsigned *type,char *name) {
 *len=1;*size=1;*type=GL_FLOAT_MAT4;strcpy(name,"m");
}
static int glGetUniformLocation(unsigned p,const char *n) {return (int)p+100;}
static int uniform_vector_should_skip(const char *n,int l,int c,const float *v) {return l<0 || c<=0 || !v;}
static int comp_fix_enabled(void) {return comp;}
static void glUniformMatrix4fv(int l,int c,int t,const float *v) {++driver_calls;memcpy(driver_value,v,64);}
'''
render+=part('#define PROGRAM_CACHE_CAP','#ifndef MCSM_FAST_FINAL_RUNTIME')
render+=part('void glUniformMatrix4fv_soloader(','void glTexStorage2D_soloader(')
render+=r'''
int main(void) {
 float m[32]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
 glUniformMatrix4fv_soloader(0,1,0,m);assert(driver_calls==1);
 for(int i=0;i<10000;++i)glUniformMatrix4fv_soloader(0,1,0,m);
 assert(driver_calls==1 && pvz2_matrix_uploads_skipped==10000);
 m[12]=42;glUniformMatrix4fv_soloader(0,1,0,m);assert(driver_calls==2 && driver_value[12]==42);
 current=2;glUniformMatrix4fv_soloader(0,1,0,m);assert(driver_calls==3);
 current=1;glUniformMatrix4fv_soloader(0,1,0,m);assert(driver_calls==3);
 program_caches_invalidate(1);glUniformMatrix4fv_soloader(0,1,0,m);assert(driver_calls==4);
 glUniformMatrix4fv_soloader(0,2,0,m);glUniformMatrix4fv_soloader(0,1,0,m);assert(driver_calls==6);
 glUniformMatrix4fv_soloader(0,1,1,m);glUniformMatrix4fv_soloader(0,1,0,m);assert(driver_calls==8);
 comp=1;glUniformMatrix4fv_soloader(0,1,0,m);glUniformMatrix4fv_soloader(0,1,0,m);assert(driver_calls==10);
 comp=0;glUniformMatrix4fv_soloader(0,1,0,m);assert(driver_calls==11);
 m[0]=INFINITY;glUniformMatrix4fv_soloader(0,1,0,m);assert(isfinite(driver_value[0]));
 current=3;program_caches_invalidate(3);int before=driver_calls;
 glUniformMatrix4fv_soloader(0,1,0,m);assert(driver_calls==before); /* No safe repair matrix. */
 m[0]=1;uniforms=2;current=4;glUniformMatrix4fv_soloader(0,1,0,m);glUniformMatrix4fv_soloader(0,1,0,m);
 assert(driver_calls==before+2); /* Multiple mat4s: ordinary driver path. */
 puts("PASS: 10000 identical sprite matrices cause one driver upload; changed matrices, programs, relink, arrays, transpose and composite path remain correct; finite recovery preserved");
}
'''
result=execute('touch',touch)+execute('render',render)
(WORK/'result.txt').write_text(result,encoding='utf-8')
print(result.strip());print(WORK)
