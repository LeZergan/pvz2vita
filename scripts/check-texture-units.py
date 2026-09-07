"""Exercise production texture upload wrappers with a tiny mocked GL driver."""
from pathlib import Path
import re
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
src=root/'vita/direct/source'
work=Path(tempfile.mkdtemp(prefix='texture-units-',dir=root/'out'))
source=(src/'utils/glutil.c').read_text(encoding='utf-8')
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
#include "utils/pixel_workers.c"
#include "utils/texture_marks.h"
int pthread_create_soloader(pthread_t *t,const pthread_attr_t_bionic *a,void *(*f)(void *),void *v) { return 1; }
typedef unsigned GLenum; typedef unsigned GLuint; typedef int GLint; typedef int GLsizei;
enum { GL_TEXTURE0=0x84c0, GL_TEXTURE_2D=0xde1, GL_ALPHA=0x1906, GL_RGBA=0x1908,
       GL_RGB=0x1907, GL_UNSIGNED_BYTE=0x1401, GL_UNSIGNED_SHORT_4_4_4_4=0x8033,
       GL_UNSIGNED_SHORT_5_6_5=0x8363, GL_BGRA_EXT=0x80e1, GL_ACTIVE_TEXTURE=1,
       GL_TEXTURE_BINDING_2D=2, GL_NO_ERROR=0, GL_OUT_OF_MEMORY=0x505,
       VGL_MEM_VRAM=0, VGL_MEM_RAM=1 };
#define MCSM_FAST_FINAL_RUNTIME 1
#define DATA_PATH "no-test-settings/"
#define GL_DIAG_TEX_UNIT_CAP 16
#define DSAMP_SLOTS 2048u
static GLenum g_diag_active_texture=GL_TEXTURE0;
static GLuint g_diag_bound_texture_2d[16],s_texlru_bound,s_dsamp[DSAMP_SLOTS],s_alpha8[DSAMP_SLOTS],s_texfail[DSAMP_SLOTS];
static unsigned active, bound[16], calls, last_id, errors;
static int last_w,last_h,last_x,last_y,fail_conversion;
static unsigned char sample[4];
static void glActiveTexture(GLenum unit) { if(unit>=GL_TEXTURE0 && unit<GL_TEXTURE0+16) active=unit-GL_TEXTURE0; }
static void glBindTexture(GLenum target,GLuint id) { if(target==GL_TEXTURE_2D) bound[active]=id; }
static void glGetIntegerv(GLenum name,GLint *v) { *v=name==GL_ACTIVE_TEXTURE ? GL_TEXTURE0+active : bound[active]; }
static void glDeleteTextures(GLsizei n,const GLuint *ids) {
    for(int i=0;i<n;++i) for(int u=0;u<16;++u) if(bound[u]==ids[i]) bound[u]=0;
}
static GLenum glGetError(void) { return 0; }
static GLenum drain_gl_errors_limited(void) { return 0; }
static void glTexSubImage2D(GLenum t,int l,int x,int y,int w,int h,GLenum f,GLenum ty,const void *p) {
    ++calls; last_id=bound[active]; last_w=w; last_h=h; last_x=x; last_y=y;
    assert(f==GL_RGBA && ty==GL_UNSIGNED_BYTE);
    if(p && w && h) memcpy(sample,p,4);
}
static void glTexImage2D(GLenum t,int l,int in,int w,int h,int border,GLenum f,GLenum ty,const void *p) {
    glTexSubImage2D(t,l,0,0,w,h,f,ty,p);
}
static void texlru_touch(GLuint t) {}
static int push_unpack_alignment_one(void) { return 4; }
static void pop_unpack_alignment(int a) {}
static void force_complete_filter(GLenum t) {}
static int texture_upload_should_log(unsigned n,GLenum e) { return 0; }
static int gl_verbose_diag_enabled(void) { return 0; }
static size_t vglMemFree(int t) { return 1000000; }
static void texture_error(GLenum e,int w,int h) { if(e) ++errors; }
#define l_info(...) ((void)0)
#define l_warn(...) ((void)0)
static uint8_t *convert_bgra_to_rgba(const uint8_t *p,int w,int h) { return NULL; }
static uint8_t *convert_rgba4444_to_rgba8888(const uint16_t *p,int w,int h) { return NULL; }
static uint8_t *convert_rgb565_to_rgba8888(const uint16_t *p,int w,int h) { return NULL; }
static uint8_t *checked_convert(const uint8_t *p,int w,int h,enum pvz2_pixel_mode m) {
    return fail_conversion ? NULL : pvz2_pixels_convert(p,w,h,m);
}
#define pvz2_pixels_convert checked_convert
'''
for name in ['gl_texture_unit_index','glActiveTexture_soloader','glBindTexture_soloader',
             'texture_sync_upload_binding','dsamp_mark','dsamp_is','alpha8_mark','alpha8_is',
             'texfail_mark','texfail_is','texture_marks_reset','glDeleteTextures_soloader',
             'downsample_rgba8888_2x','glTexImage2D_pvz2_impl','glTexSubImage2D_soloader']:
    c+=function(name)
c+=r'''
int main(void) {
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
    puts("PASS: active-unit format/size/failure metadata, raw GL reconciliation, fused subimages, deleted-ID reuse, thin textures and allocation-failure guards");
}
'''
(work/'check.c').write_text(c,encoding='utf-8')
exe=work/'check.exe'
subprocess.run(['gcc','-std=gnu11','-O2','-static','-pthread','-DPVZ2_PIXEL_HOST_TEST',
                '-I'+str(src),str(work/'check.c'),'-o',str(exe)],check=True,timeout=30)
run=subprocess.run([str(exe)],check=True,capture_output=True,text=True,timeout=5)
(work/'result.txt').write_text(run.stdout,encoding='utf-8')
print(run.stdout.strip()); print(work)
