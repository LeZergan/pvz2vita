"""Small, noninteractive check of the production audio queue, PCM and IME code.

No emulator, windows, desktop control or audio output. Vita calls are mocked.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = root / 'vita/direct/source'
work = Path(tempfile.mkdtemp(prefix='audio-input-', dir=root / 'out'))
sdk = Path('C:/Users/Max/tools/vitasdk/arm-vita-eabi/include')
shutil.copytree(sdk / 'SLES', work / 'SLES')
(work / 'psp2/kernel').mkdir(parents=True)
(work / 'psp2/kernel/threadmgr.h').write_text('''
#include <stdint.h>
#include <time.h>
#include <unistd.h>
typedef int SceUID;
static int sceKernelGetThreadId(void) {return 1;}
static int sceKernelChangeThreadCpuAffinityMask(int a,int b) {return 0;}
static int sceKernelGetThreadCpuAffinityMask(int a) {return 0x40000;}
static int sceKernelChangeThreadPriority(int a,int b) {return 0;}
static uint64_t sceKernelGetSystemTimeWide(void) {
 struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
 return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;
}
''', encoding='utf-8')
(work / 'psp2/sysmodule.h').write_text('''
#define SCE_SYSMODULE_IME 1
static int sceSysmoduleLoadModule(int x) { return 0; }
''', encoding='utf-8')
(work / 'psp2/ime_dialog.h').write_text('''
#include <stdint.h>
typedef uint16_t SceWChar16;
typedef struct { int type, maxTextLength, dialogMode;
 const SceWChar16 *title, *initialText; SceWChar16 *inputTextBuffer; } SceImeDialogParam;
typedef struct { int result, button; } SceImeDialogResult;
#define SCE_IME_TYPE_NUMBER 1
#define SCE_IME_TYPE_DEFAULT 0
#define SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL 0
#define SCE_COMMON_DIALOG_STATUS_FINISHED 2
#define SCE_IME_DIALOG_BUTTON_ENTER 1
static int ime_status, ime_button=1, ime_init_rc, releases;
static SceWChar16 *ime_buffer;
static void sceImeDialogParamInit(SceImeDialogParam *p) { memset(p,0,sizeof(*p)); }
static int sceImeDialogInit(SceImeDialogParam *p) { ime_buffer=p->inputTextBuffer; return ime_init_rc; }
static int sceImeDialogGetStatus(void) { return ime_status; }
static int sceImeDialogGetResult(SceImeDialogResult *r) { r->result=0; r->button=ime_button; return 0; }
static void sceImeDialogAbort(void) { ime_status=2; }
static void sceImeDialogTerm(void) { ime_status=0; }
void controls_release_for_dialog(void) { ++releases; }
''', encoding='utf-8')
source = (src / 'reimpl/opensl_audio.c').read_text(encoding='utf-8')
ids = sorted(set(re.findall(r'\bSL_IID_[A-Z]+\b', source)))
test = r'''
#include <assert.h>
#include <stdio.h>
#include "utils/pcm_blocks.h"
#include "utils/ime_utf8.h"
#include "utils/text_field_452.h"
#include "reimpl/opensl_audio.c"
void telemetry_log(const char *t,const char *f,...) {}
void _log_print(int t,const char *f,...) {}
static atomic_uint submitted, callbacks, block_output, entered_output;
static PcmBlocks pcm;
static int16_t recorded[16384];
static unsigned recorded_n, block_size=1024, channels=2;
static int emit(const int16_t *data, void *ctx) {
    assert(recorded_n+block_size*channels <= 16384);
    memcpy(recorded+recorded_n,data,block_size*channels*2);
    recorded_n += block_size*channels;
    return 0;
}
void audio_reset_stream(void) {pcm.pending=0;}
void audio_close_port(void) {audio_reset_stream();}
int audio_open_port(int rate,int ch,int size) {return size;}
void audio_output_i16_frames(const int16_t *data,int frames,int ch) {
    atomic_store(&entered_output,1);
    while(atomic_load(&block_output)) usleep(1000);
    atomic_fetch_add(&submitted,1);
}
static void done(SLBufferQueueItf q,void *ctx) {atomic_fetch_add(&callbacks,1);}
static void wait_for(atomic_uint *a,unsigned wanted) {
    for(unsigned i=0;i<1000 && atomic_load(a)<wanted;++i) usleep(1000);
    assert(atomic_load(a)>=wanted);
}
static void *clear_thread(void *ctx) {player_bq_clear(NULL); atomic_store((atomic_uint *)ctx,1); return NULL;}
static void test_queue(void) {
    SLDataFormat_PCM format={.formatType=SL_DATAFORMAT_PCM,.numChannels=2,
      .samplesPerSec=32000000,.bitsPerSample=16};
    SLDataLocator_BufferQueue locator={SL_DATALOCATOR_BUFFERQUEUE,2};
    SLDataSource source={&locator,&format};
    SLObjectItf obj;
    assert(eng_create_audio_player(NULL,&obj,&source,NULL,0,NULL,NULL)==SL_RESULT_SUCCESS);
    int16_t buffer[2048]={0};
    player_bq_register_callback(NULL,done,NULL);
    assert(player_enqueue_common(g_player,buffer,3)==SL_RESULT_PARAMETER_INVALID);
    assert(player_enqueue_common(g_player,buffer,1024)==SL_RESULT_SUCCESS);
    assert(player_enqueue_common(g_player,buffer,2048)==SL_RESULT_SUCCESS);
    assert(atomic_load(&submitted)==0); /* Enqueue must never call blocking output. */
    assert(player_enqueue_common(g_player,buffer,1024)==SL_RESULT_BUFFER_INSUFFICIENT);
    SLBufferQueueState state;
    player_bq_get_state(NULL,&state); assert(state.count==2 && state.playIndex==0);
    assert(player_play_set_state(NULL,SL_PLAYSTATE_PLAYING)==SL_RESULT_SUCCESS);
    wait_for(&callbacks,2);
    player_play_set_state(NULL,SL_PLAYSTATE_PAUSED);
    SLuint32 play; player_play_get_state(NULL,&play); assert(play==SL_PLAYSTATE_PAUSED);
    assert(player_enqueue_common(g_player,buffer,1024)==SL_RESULT_SUCCESS);
    usleep(5000); assert(atomic_load(&submitted)==2);
    player_play_set_state(NULL,SL_PLAYSTATE_PLAYING);
    wait_for(&callbacks,3);
    /* Clear must wait for in-flight output before returning borrowed buffers. */
    atomic_store(&block_output,1); atomic_store(&entered_output,0);
    assert(player_enqueue_common(g_player,buffer,1024)==SL_RESULT_SUCCESS);
    wait_for(&entered_output,1);
    atomic_uint cleared=0; pthread_t clearer;
    assert(!pthread_create(&clearer,NULL,clear_thread,&cleared));
    usleep(5000); assert(!atomic_load(&cleared));
    atomic_store(&block_output,0); pthread_join(clearer,NULL);
    assert(atomic_load(&cleared));
    player_bq_get_state(NULL,&state); assert(state.count==0 && state.playIndex==0);
    player_play_set_state(NULL,SL_PLAYSTATE_STOPPED);
    obj_destroy(obj); assert(!g_player);
}
static void test_pcm(void) {
    int16_t wave[8192]; for(unsigned i=0;i<8192;i++) wave[i]=(int16_t)(i-4000);
    /* Deliberately cross both packet and output-block boundaries. */
    unsigned sizes[]={1,255,512,257,1023,2048}, offset=0;
    for(unsigned i=0;i<6;i++) {
        assert(!pcm_blocks_write(&pcm,wave+offset*2,sizes[i],2,1024,256,emit,NULL));
        offset+=sizes[i];
    }
    assert(offset==4096 && recorded_n==8192 && !pcm.pending);
    assert(!memcmp(recorded,wave,sizeof(wave))); /* No silence, drops or repetition. */
    memset(&pcm,0,sizeof(pcm)); recorded_n=0; channels=1; block_size=64;
    int16_t loud[64]; for(unsigned i=0;i<64;i++) loud[i]=(i&1)?-30000:30000;
    pcm_blocks_write(&pcm,loud,63,1,64,512,emit,NULL); assert(!recorded_n);
    pcm_blocks_write(&pcm,loud+63,1,1,64,512,emit,NULL);
    for(unsigned i=0;i<64;i++) assert(recorded[i]==((i&1)?-32768:32767));
    assert(!pcm_blocks_write(&pcm,loud,20,1,64,256,emit,NULL));
    audio_reset_stream(); assert(!pcm.pending);
}
static void test_text(void) {
    unsigned char field[256]={0}; uint32_t word;
    field[0x88]=1; word=12; memcpy(field+0x8c,&word,4); /* Existing prompt value. */
    assert(text452_select_all(field));
    memcpy(&word,field+0xc8,4); assert(word==12);
    memcpy(&word,field+0xcc,4); assert(word==0);
    field[0x88]=2; assert(text452_select_all(field)); /* Short one-character value. */
    memcpy(&word,field+0xc8,4); assert(word==1);
    field[0x88]=0; assert(text452_select_all(field));
    unsigned char same[12]={0}; assert(text452_equal(field+0x88,same));
    field[0x88]=same[0]=2; field[0x8c]=same[4]='A';
    assert(text452_equal(field+0x88,same)); same[4]='B'; assert(!text452_equal(field+0x88,same));
    field[0x88]=1; word=65536; memcpy(field+0x8c,&word,4); assert(!text452_select_all(field));
    uint16_t text[]={'A',0x0416,0x4e2d,0xd83c,0xdf31,0}; char out[257];
    assert(ime_utf8(text,6,out,sizeof(out))==10);
    assert(!strcmp(out,"A\xd0\x96\xe4\xb8\xad\xf0\x9f\x8c\xb1"));
    assert(ime_utf8(text,6,out,4)==3 && !strcmp(out,"A\xd0\x96"));
    uint16_t bad[]={0xd800,'B',0xdc00,0};
    assert(ime_utf8(bad,4,out,sizeof(out))==7);
    assert(!strcmp(out,"\xef\xbf\xbd" "B" "\xef\xbf\xbd"));
    uint16_t full[65]; for(unsigned i=0;i<64;i++) full[i]=0x4e2d; full[64]=0;
    assert(ime_utf8(full,64,out,sizeof(out))==192);
}
int main(void) {
    test_pcm(); test_text(); test_queue();
    puts("PASS: PCM continuity/clipping/reset; UTF-16 conversion/bounds; nonblocking queue/full/state/callback/pause/resume/clear/stop/destroy");
}
'''
# Exercise the exact Java input packet writer as well as the UTF converter.
java = (src / 'java.c').read_text(encoding='utf-8')
a = java.index('#define PVZ2_INPUT_QUEUE_CAP')
b = java.index('\n}', java.index('static unsigned input_drain_to_buffer', a)) + 3
packet_code = '\n#include <sys/time.h>\n#include "java_runtime.h"\n#include "reimpl/numeric_input.c"\n' + java[a:b]
test = test.replace('static void test_text(void) {', packet_code + '\nstatic void test_text(void) {')
test = test.replace('assert(ime_utf8(full,64,out,sizeof(out))==192);', r'''assert(ime_utf8(full,64,out,sizeof(out))==192);
    pvz2_input_push_text(out); pvz2_input_push_key(67,1); pvz2_input_push_key(67,0);
    unsigned char packet[1024];
    assert(input_drain_to_buffer(packet,200)==0); /* Leave an unfit record queued. */
    assert(input_drain_to_buffer(packet,sizeof(packet))==3);
    assert(packet[0]==3 && packet[16]==6 && packet[20]==0 && packet[24]==192);
    assert(!memcmp(packet+28,out,192));
    assert(packet[220]==1 && packet[224]==67);
    assert(packet[228]==0 && packet[232]==0); /* Unicode=0, ACTION_DOWN=0. */
    assert(packet[252]==1 && packet[256]==67 && packet[260]==0 && packet[264]==1);
    assert(input_drain_to_buffer(packet,sizeof(packet))==0);
    pvz2_text_request(); assert(pvz2_keyboard_is_showing());
    assert(pvz2_numeric_poll() && releases==1);
    ime_buffer[0]='A'; ime_buffer[1]=0x0416; ime_buffer[2]=0;
    ime_status=2; assert(pvz2_numeric_poll());
    assert(!pvz2_numeric_active() && pvz2_keyboard_is_showing());
    pvz2_keyboard_after_frame(); assert(pvz2_keyboard_is_showing()); /* Not delivered. */
    assert(input_drain_to_buffer(packet,sizeof(packet))==1);
    assert(packet[24]==3 && !memcmp(packet+28,"A\xd0\x96",3));
    assert(input_drain_to_buffer(packet,sizeof(packet))==0);
    assert(pvz2_keyboard_is_showing()); /* Native dispatch still has focus this frame. */
    pvz2_keyboard_after_frame(); assert(!pvz2_keyboard_is_showing());
    pvz2_text_request(); assert(pvz2_numeric_poll());
    ime_button=0; ime_status=2; assert(pvz2_numeric_poll());
    assert(!pvz2_keyboard_is_showing() && !input_drain_to_buffer(packet,sizeof(packet)));
    pvz2_numeric_request(1); assert(pvz2_numeric_poll());
    ime_buffer[0]='4'; ime_buffer[1]='x'; ime_buffer[2]='2'; ime_buffer[3]=0;
    ime_button=1; ime_status=2; assert(pvz2_numeric_poll());
    assert(input_drain_to_buffer(packet,sizeof(packet))==1);
    assert(packet[24]==2 && !memcmp(packet+28,"42",2));
    pvz2_keyboard_after_frame(); assert(!pvz2_keyboard_is_showing());
    pvz2_text_request(); assert(pvz2_numeric_poll());
    ime_buffer[0]=0; ime_status=2; assert(pvz2_numeric_poll());
    assert(pvz2_keyboard_commit_pending());
    assert(input_drain_to_buffer(packet,sizeof(packet))==1 && packet[24]==0);
    pvz2_keyboard_after_frame(); assert(!pvz2_keyboard_commit_pending());
    ime_init_rc=-1; pvz2_text_request(); assert(!pvz2_numeric_poll());
    assert(!pvz2_keyboard_is_showing());
    puts("PASS: exact 4.5.2 whole-field selection; empty confirmation delivered; IME accept/cancel/failure, numeric filtering, commit visibility; exact Android key-down/up packets");''')
test += '\n'.join(f'const SLInterfaceID {name} = NULL;' for name in ids)
(work / 'check.c').write_text(test, encoding='utf-8')
exe = work / 'check.exe'
subprocess.run(['gcc', '-std=gnu11', '-O2', '-static', '-pthread', '-I'+str(work),
                '-I'+str(src), str(work/'check.c'), '-o', str(exe)], check=True, timeout=30)
run = subprocess.run([str(exe)], check=True, capture_output=True, text=True, timeout=8)
(work / 'result.txt').write_text(run.stdout, encoding='utf-8')
print(run.stdout.strip())
print(work)
