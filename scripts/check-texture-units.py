"""Exercise production texture upload wrappers with a tiny mocked GL driver."""
from pathlib import Path
import re
import argparse
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
src=root/'vita/direct/source'
work=Path(tempfile.mkdtemp(prefix='texture-units-',dir=root/'out'))
parser=argparse.ArgumentParser();parser.add_argument('--source',type=Path,default=src/'utils/glutil.c');parser.add_argument('--case',default='all');args=parser.parse_args()
source=args.source.read_text(encoding='utf-8')
def function(name):
    m=re.search(r'(?m)^(?:static )?(?:void|int|uint8_t\s*\*)\s*'+name+r'\([^;]*?\)\s*\{',source)
    assert m,name
    start=m.start(); brace=m.end()-1; depth=1; end=brace+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}'); end+=1
    return source[start:end]+'\n'
c=r'''
#include <assert.h>
#include <string.h>
#include <setjmp.h>
/* This texture-state fixture uses the synchronous conversion fallback. The
 * separate pixel-worker suite exercises real concurrent host notifications. */
typedef int SceUID;
#define SCE_KERNEL_ERROR_WAIT_TIMEOUT (-110)
static int sceKernelCreateSema(const char *name,int attr,int initial,int maximum,void *options) { return -1; }
static int sceKernelDeleteSema(SceUID id) { return 0; }
static int sceKernelWaitSema(SceUID id,int count,unsigned *timeout) { assert(0); return -110; }
static int sceKernelSignalSema(SceUID id,int count) { assert(0); return -1; }
static int sceKernelDelayThread(unsigned us) { assert(0); return 0; }
#include "utils/pixel_workers.c"
#include "utils/texture_marks.h"
int pthread_create_soloader(pthread_t *t,const pthread_attr_t_bionic *a,void *(*f)(void *),void *v) { return 1; }
typedef unsigned GLenum; typedef unsigned GLuint; typedef int GLint; typedef int GLsizei;
enum { GL_TEXTURE0=0x84c0, GL_TEXTURE_2D=0xde1, GL_ALPHA=0x1906, GL_RGBA=0x1908,
       GL_RGB=0x1907, GL_UNSIGNED_BYTE=0x1401, GL_UNSIGNED_SHORT_4_4_4_4=0x8033,
       GL_UNSIGNED_SHORT_5_6_5=0x8363, GL_BGRA_EXT=0x80e1, GL_ACTIVE_TEXTURE=1,
       GL_TEXTURE_BINDING_2D=2, GL_NO_ERROR=0, GL_OUT_OF_MEMORY=0x505,
       VGL_MEM_VRAM=0, VGL_MEM_RAM=1, GL_INVALID_VALUE=0x501, GL_ETC1_RGB8_OES=0x8d64 };
#define MCSM_FAST_FINAL_RUNTIME 1
#define DATA_PATH "no-test-settings/"
#define GL_DIAG_TEX_UNIT_CAP 16
#define DSAMP_SLOTS 2048u
static GLenum g_diag_active_texture=GL_TEXTURE0;
static GLuint g_diag_bound_texture_2d[16],s_texlru_bound,s_dsamp[DSAMP_SLOTS],s_alpha8[DSAMP_SLOTS],s_texfail[DSAMP_SLOTS];
static unsigned active, bound[16], calls, last_id, errors, fail_uploads, image_calls, last_null;
static GLenum pending_error;
static int fatal_expected;
static unsigned fatal_image_limit;
static void fatal_error(const char *fmt,...) { assert(fatal_expected && image_calls==fatal_image_limit); puts("PASS: unrecoverable allocation stops before invalid storage is used");exit(0); }
#define telemetry_log(...) ((void)0)
static int last_w,last_h,last_x,last_y,fail_conversion;
static unsigned char sample[4];
static void glActiveTexture(GLenum unit) { if(unit>=GL_TEXTURE0 && unit<GL_TEXTURE0+16) active=unit-GL_TEXTURE0; }
static void glBindTexture(GLenum target,GLuint id) { if(target==GL_TEXTURE_2D) bound[active]=id; }
static void glGetIntegerv(GLenum name,GLint *v) { *v=name==GL_ACTIVE_TEXTURE ? GL_TEXTURE0+active : bound[active]; }
static void glDeleteTextures(GLsizei n,const GLuint *ids) {
    for(int i=0;i<n;++i) for(int u=0;u<16;++u) if(bound[u]==ids[i]) bound[u]=0;
}
static GLenum glGetError(void) { GLenum e=pending_error;pending_error=0;return e; }
static GLenum drain_gl_errors_limited(void) { return glGetError(); }
static void glTexSubImage2D(GLenum t,int l,int x,int y,int w,int h,GLenum f,GLenum ty,const void *p) {
    ++calls; last_id=bound[active]; last_w=w; last_h=h; last_x=x; last_y=y;
    assert(f==GL_RGBA && ty==GL_UNSIGNED_BYTE);
    if(p && w && h) memcpy(sample,p,4);
}
static void glTexImage2D(GLenum t,int l,int in,int w,int h,int border,GLenum f,GLenum ty,const void *p) {
    ++image_calls;last_null=!p;
    glTexSubImage2D(t,l,0,0,w,h,f,ty,p);
    if(fail_uploads) { --fail_uploads;pending_error=GL_OUT_OF_MEMORY; }
}
static void texlru_touch(GLuint t) {}
static int push_unpack_alignment_one(void) { return 4; }
static void pop_unpack_alignment(int a) {}
static unsigned complete_filters;
static void force_complete_filter(GLenum t) { ++complete_filters; }
static int texture_upload_should_log(unsigned n,GLenum e) { return 0; }
static int gl_verbose_diag_enabled(void) { return 0; }
static size_t vglMemFree(int t) { return 1000000; }
static void texture_error(GLenum e,int w,int h) { if(e) ++errors; }
#define l_info(...) ((void)0)
#define l_warn(...) ((void)0)
static uint8_t *convert_bgra_to_rgba(const uint8_t *p,int w,int h) { return NULL; }


static uint8_t *checked_convert(const uint8_t *p,int w,int h,enum pvz2_pixel_mode m) {
    return fail_conversion ? NULL : pvz2_pixels_convert(p,w,h,m);
}
#define pvz2_pixels_convert checked_convert
'''
for name in ['rgba8888_byte_count','convert_rgba4444_to_rgba8888','convert_rgb565_to_rgba8888','gl_texture_unit_index','glActiveTexture_soloader','glBindTexture_soloader',
             'texture_sync_upload_binding','dsamp_mark','dsamp_is','alpha8_mark','alpha8_is',
             'texfail_mark','texfail_is','texture_marks_reset','glDeleteTextures_soloader',
             'downsample_rgba8888_2x',*(['texture_reduce_dimensions'] if 'static int texture_reduce_dimensions(' in source else []),'glTexImage2D_pvz2_impl','glTexSubImage2D_soloader']:
    if name=='glTexImage2D_pvz2_impl' and 'static void texture_placeholder(' in source:c+=function('texture_placeholder')
    c+=function(name)
c+=source[(source.index('static const int k_etc1_mod') if 'static const int k_etc1_mod' in source else source.index('static uint8_t *g_etc1_scratch')):(source.index('static int texture_reduce_dimensions(') if 'static int texture_reduce_dimensions(' in source else source.index('static void texture_marks_reset(GLuint id);'))]
c+=r'''
static int gl_get_int_for_diag(GLenum n,GLenum *err) { return bound[active]; }
static void gl_tex_mark_pot(GLuint id,int w,int h) {}
static void mcsm_scene_load_tick(void) {}
static void texlru_before_upload(int id,int size) {}
static void texlru_after_upload(int id,int size,GLenum err) {}
static void glCompressedTexImage2D(GLenum t,int l,GLenum f,int w,int h,int b,int n,const void *p) { assert(0); }
'''
if 'static int etc1_payload_valid(' in source:c+=function('etc1_payload_valid')
c+=function('glCompressedTexImage2D_soloader')+function('glCompressedTexSubImage2D_soloader')
c+=r'''
int main(int argc,char **argv) {
    const char *scenario=argc>1 ? argv[1] : "all";
    if(!strcmp(scenario,"all") || !strcmp(scenario,"etc1-origin") || !strcmp(scenario,"etc1-offset")) {
        unsigned char blocks[128]={0};
        glBindTexture_soloader(GL_TEXTURE_2D,22);
        glCompressedTexImage2D_soloader(GL_TEXTURE_2D,0,GL_ETC1_RGB8_OES,16,16,0,128,blocks);
        unsigned images=image_calls, updates=calls;
        int offset=!strcmp(scenario,"etc1-offset");
        glCompressedTexSubImage2D_soloader(GL_TEXTURE_2D,0,offset?4:0,offset?8:0,8,8,GL_ETC1_RGB8_OES,32,blocks);
        assert(image_calls==images && calls==updates+1 && last_x==(offset?4:0) && last_y==(offset?8:0));
    }
    if(!strcmp(scenario,"all") || !strcmp(scenario,"etc1")) {
        unsigned char *blocks=calloc(1,2048*2048/2);assert(blocks);
        glBindTexture_soloader(GL_TEXTURE_2D,23);
        glCompressedTexImage2D_soloader(GL_TEXTURE_2D,0,GL_ETC1_RGB8_OES,2048,2048,0,2048*2048/2,blocks);
        assert(last_w==1024 && last_h==1024 && dsamp_is(23));
        if(!strcmp(scenario,"all")) assert(g_etc1_scratch_sz==4u*1024*1024);
        assert(sample[0]==2 && sample[1]==2 && sample[2]==2 && sample[3]==255);
        unsigned images=image_calls;
        glCompressedTexSubImage2D_soloader(GL_TEXTURE_2D,0,0,0,8,8,GL_ETC1_RGB8_OES,32,blocks);
        assert(image_calls==images && last_w==4 && dsamp_is(23));
        glCompressedTexSubImage2D_soloader(GL_TEXTURE_2D,0,8,12,8,8,GL_ETC1_RGB8_OES,32,blocks);
        assert(image_calls==images && last_x==4 && last_y==6 && last_w==4);
        unsigned before=calls;
        glCompressedTexImage2D_soloader(GL_TEXTURE_2D,0,GL_ETC1_RGB8_OES,8,8,0,1,blocks);
        assert(calls==before); /* Truncated payload must never reach decoder. */
        fail_uploads=1;
        glCompressedTexImage2D_soloader(GL_TEXTURE_2D,0,GL_ETC1_RGB8_OES,2048,2048,0,2048*2048/2,blocks);
        assert(last_w==1 && texfail_is(23));
        glCompressedTexImage2D_soloader(GL_TEXTURE_2D,0,GL_ETC1_RGB8_OES,2048,2048,0,2048*2048/2,NULL);
        assert(last_w==1024 && last_null && dsamp_is(23) && !texfail_is(23));
        glCompressedTexSubImage2D_soloader(GL_TEXTURE_2D,0,0,0,2048,2048,GL_ETC1_RGB8_OES,2048*2048/2,blocks);
        assert(last_w==1024 && g_etc1_scratch_sz==4u*1024*1024);
        free(blocks);errors=0;
        puts("PASS: ETC1 pixels; 4MiB CPU/GPU atlas storage, NULL allocation/full fill, origin/offset updates, payload bounds and recovery");
    }
    if(!strcmp(scenario,"all") || !strcmp(scenario,"half-filter")) {
        uint8_t *large=calloc(1024*1024,4);assert(large);
        glBindTexture_soloader(GL_TEXTURE_2D,19);
        unsigned filters=complete_filters;
        glTexImage2D_pvz2_impl(GL_TEXTURE_2D,0,GL_RGBA,1024,1024,0,GL_RGBA,GL_UNSIGNED_BYTE,large);
        assert(dsamp_is(19) && complete_filters>filters);
        filters=complete_filters;fail_uploads=1;
        glTexImage2D_pvz2_impl(GL_TEXTURE_2D,0,GL_RGBA,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,large);
        assert(dsamp_is(19) && complete_filters>filters);
        free(large);errors=0;
    }
    uint8_t rgba[8*8*4], alpha[8*8];
    memset(rgba,91,sizeof(rgba)); for(int i=0;i<64;++i) alpha[i]=i;
    glBindTexture_soloader(GL_TEXTURE_2D,11);
    glTexImage2D_pvz2_impl(GL_TEXTURE_2D,0,GL_RGBA,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    glActiveTexture_soloader(GL_TEXTURE0+1); glBindTexture_soloader(GL_TEXTURE_2D,12);
    glTexImage2D_pvz2_impl(GL_TEXTURE_2D,0,GL_ALPHA,1024,1024,0,GL_ALPHA,GL_UNSIGNED_BYTE,NULL);
    assert(alpha8_is(12) && dsamp_is(12) && last_w==512);
    glActiveTexture_soloader(GL_TEXTURE0); assert(s_texlru_bound==11);
    glTexSubImage2D_soloader(GL_TEXTURE_2D,0,0,0,8,8,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    assert(last_id==11 && last_w==8 && last_h==8 && sample[0]==91);
    glActiveTexture_soloader(GL_TEXTURE0+1); assert(s_texlru_bound==12);
    glTexSubImage2D_soloader(GL_TEXTURE_2D,0,4,6,8,8,GL_ALPHA,GL_UNSIGNED_BYTE,alpha);
    assert(last_id==12 && last_w==4 && last_h==4 && last_x==2 && last_y==3);
    assert(sample[0]==255 && sample[3]==4); /* fused alpha is halved exactly once */
    glActiveTexture_soloader(GL_TEXTURE0); glBindTexture_soloader(GL_TEXTURE_2D,13); texfail_mark(13);
    glActiveTexture_soloader(GL_TEXTURE0+1);
    unsigned before=calls;
    glTexSubImage2D_soloader(GL_TEXTURE_2D,0,0,0,8,8,GL_ALPHA,GL_UNSIGNED_BYTE,alpha);
    assert(calls==before+1); /* other unit's failed allocation must not hide this fill */
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,11); /* raw loader GL */
    glTexSubImage2D_soloader(GL_TEXTURE_2D,0,0,0,8,8,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    assert(last_id==11 && last_w==8 && g_diag_active_texture==GL_TEXTURE0);
    glActiveTexture_soloader(GL_TEXTURE0+1);
    GLuint id=12; glDeleteTextures_soloader(1,&id);
    assert(!s_texlru_bound && !alpha8_is(12) && !dsamp_is(12));
    glBindTexture_soloader(GL_TEXTURE_2D,12);
    glTexImage2D_pvz2_impl(GL_TEXTURE_2D,0,GL_RGBA,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    assert(!alpha8_is(12) && !dsamp_is(12));
    alpha8_mark(12); dsamp_mark(12); fail_conversion=1; before=calls;
    glTexSubImage2D_soloader(GL_TEXTURE_2D,0,0,0,8,8,GL_ALPHA,GL_UNSIGNED_BYTE,alpha);
    assert(calls==before && errors==1); /* never upload full-size or short A8 data on OOM */
    fail_conversion=0;
    glTexImage2D_pvz2_impl(GL_TEXTURE_2D,0,GL_ALPHA,1024,1,0,GL_ALPHA,GL_UNSIGNED_BYTE,NULL);
    assert(last_w==1024 && last_h==1 && !dsamp_is(12));

    if(!strcmp(scenario,"all") || !strcmp(scenario,"mips")) {
        before=calls;dsamp_mark(12);
        glTexImage2D_pvz2_impl(GL_TEXTURE_2D,1,GL_RGBA,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
        assert(calls==before);
        texture_marks_reset(12);
        glTexImage2D_pvz2_impl(GL_TEXTURE_2D,1,GL_RGBA,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
        assert(calls==before+1);
    }
    if(!strcmp(scenario,"all") || !strcmp(scenario,"packed")) {
        uint16_t packed[16];for(unsigned i=0;i<16;i++)packed[i]=0xf00f;
        glTexImage2D_pvz2_impl(GL_TEXTURE_2D,0,GL_RGBA,4,4,0,GL_RGBA,GL_UNSIGNED_SHORT_4_4_4_4,NULL);
        glTexSubImage2D_soloader(GL_TEXTURE_2D,0,0,0,4,4,GL_RGBA,GL_UNSIGNED_SHORT_4_4_4_4,packed);
        assert(sample[0]==255 && !sample[1] && !sample[2] && sample[3]==255);
        for(unsigned i=0;i<16;i++)packed[i]=0x07e0;
        glTexImage2D_pvz2_impl(GL_TEXTURE_2D,0,GL_RGB,4,4,0,GL_RGB,GL_UNSIGNED_SHORT_5_6_5,packed);
        assert(!sample[0] && sample[1]==255 && !sample[2] && sample[3]==255);
        glTexSubImage2D_soloader(GL_TEXTURE_2D,0,0,0,4,4,GL_RGB,GL_UNSIGNED_SHORT_5_6_5,packed);
        assert(!sample[0] && sample[1]==255 && !sample[2] && sample[3]==255);
    }
    if(!strcmp(scenario,"all") || !strcmp(scenario,"oom-conversion")) {
        unsigned char big[64*64*4];memset(big,1,sizeof(big));
        fail_uploads=1;fail_conversion=1;
        glTexImage2D_pvz2_impl(GL_TEXTURE_2D,0,GL_RGBA,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,big);
        assert(last_w==1 && last_h==1 && !last_null && texfail_is(12) && !dsamp_is(12));
        before=calls;
        glTexSubImage2D_soloader(GL_TEXTURE_2D,0,0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,big);
        glTexImage2D_pvz2_impl(GL_TEXTURE_2D,1,GL_RGBA,32,32,0,GL_RGBA,GL_UNSIGNED_BYTE,big);
        assert(calls==before);fail_conversion=0;
    }
    if(!strcmp(scenario,"all") || !strcmp(scenario,"oom-alpha")) {
        fail_uploads=1;
        glTexImage2D_pvz2_impl(GL_TEXTURE_2D,0,GL_ALPHA,1024,1024,0,GL_ALPHA,GL_UNSIGNED_BYTE,NULL);
        assert(last_w==1 && last_h==1 && texfail_is(12) && !alpha8_is(12) && !dsamp_is(12));
    }
    if(!strcmp(scenario,"oom-target")) {
        fail_uploads=1;fatal_expected=1;before=image_calls;fatal_image_limit=image_calls+1;
        {
            glTexImage2D_pvz2_impl(GL_TEXTURE_2D,0,GL_RGBA,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
            assert(!"failed render target silently resized");
        }
        fatal_expected=0;assert(image_calls==before+1 && last_w==64 && last_h==64);
    }
    if(!strcmp(scenario,"oom-placeholder")) {
        unsigned char big[64*64*4];memset(big,1,sizeof(big));
        fail_uploads=3;fatal_expected=1;fatal_image_limit=image_calls+3;
        {
            glTexImage2D_pvz2_impl(GL_TEXTURE_2D,0,GL_RGBA,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,big);
            assert(!"failed placeholder returned to renderer");
        }
        fatal_expected=0;
    }
    puts("PASS: active-unit format/size/failure metadata, raw GL reconciliation, fused subimages, deleted-ID reuse, thin textures and allocation-failure guards");
}
'''
(work/'check.c').write_text(c,encoding='utf-8')
exe=work/'check.exe'
subprocess.run(['gcc','-std=gnu11','-O2','-static','-pthread','-DPVZ2_PIXEL_HOST_TEST',
                '-I'+str(src),str(work/'check.c'),'-o',str(exe)],check=True,timeout=30)
outputs=[]
for case in (['all','oom-target','oom-placeholder'] if args.case=='all' else [args.case]):
    run=subprocess.run([str(exe),case],capture_output=True,text=True,timeout=5)
    if run.returncode: print(run.stdout+run.stderr);run.check_returncode()
    outputs.append(run.stdout)
(work/'result.txt').write_text(''.join(outputs),encoding='utf-8')
print(''.join(outputs).strip());print(work)
