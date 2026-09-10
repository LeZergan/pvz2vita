"""Exercise production pair-cache ownership, key boundaries and failure paths."""
from pathlib import Path
import subprocess, tempfile
r = Path(__file__).resolve().parents[1]
w = Path(tempfile.mkdtemp(prefix='shader-pairs-', dir=r/'out'))
c = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned GLuint, GLenum;
typedef int GLint, GLsizei;
#define GL_FALSE 0
#define GL_LINK_STATUS 1
#define GL_SHADER_TYPE 2
#define GL_VERTEX_SHADER 3
#define GL_FRAGMENT_SHADER 4
#define PVZ2_VGL_PROGRAM_LIMIT 1024u
typedef struct { char *owned_source; size_t owned_source_len; } shader_diag_entry;
static shader_diag_entry diag[2048];
static struct { unsigned live, refs, dirty, compiled, type; } sh[2048];
static struct { unsigned live, linked, shaders[2], attribute; } pr[1025];
static unsigned compiles, links, fail_alloc, fail_program, fail_link, oversized, writes;
static unsigned pvz2_pair_hits, pvz2_pair_misses, pvz2_pair_bypasses;
static void *test_malloc(size_t n) { return fail_alloc ? NULL : malloc(n); }
#define malloc test_malloc
static shader_diag_entry *get_shader_diag_entry(GLuint s, int create) { return &diag[s]; }
static GLuint glCreateProgram(void) {
    if(fail_program) return fail_program;
    for(unsigned p=1;p<=1024;++p) if(!pr[p].live) { memset(&pr[p],0,sizeof(pr[p])); pr[p].live=1; return p; }
    return 0;
}
static int glIsProgram(GLuint p) { assert(p && p<=1024); return pr[p].live; }
static void glGetAttachedShaders(GLuint p,GLsizei n,GLsizei *count,GLuint *out) {
    assert(n==2); *count=0;
    for(unsigned i=0;i<2;++i) if(pr[p].shaders[i]) out[(*count)++]=pr[p].shaders[i];
}
static void glGetShaderiv(GLuint s,GLenum name,GLint *out) { assert(name==GL_SHADER_TYPE);*out=sh[s].type; }
static void glGetProgramiv(GLuint p,GLenum name,GLint *out) {
    *out=name==GL_LINK_STATUS ? pr[p].linked : oversized ? 2*1024*1024 : 1000;
}
static void unref(GLuint s) { assert(sh[s].refs); if(!--sh[s].refs && sh[s].dirty) sh[s].live=0; }
static void glAttachShader(GLuint p,GLuint s) {
    assert(pr[p].live && !pr[p].linked && sh[s].live);
    unsigned i=sh[s].type==GL_VERTEX_SHADER ? 0:1;
    ++sh[s].refs; if(pr[p].shaders[i]) unref(pr[p].shaders[i]); pr[p].shaders[i]=s;
}
static void glLinkProgram(GLuint p) {
    ++links; if(fail_link || pr[p].linked) return;
    for(unsigned i=0;i<2;++i) { unsigned s=pr[p].shaders[i]; if(s && !sh[s].compiled) { ++compiles; sh[s].compiled=1; } }
    pr[p].linked=1;
}
#include "utils/shader_pairs.h"
static GLuint shader(unsigned type,const char *source) {
    for(unsigned s=1;s<2048;++s) if(!sh[s].live) {
        memset(&sh[s],0,sizeof(sh[s]));sh[s].live=1;sh[s].type=type;
        diag[s]=(shader_diag_entry){(char*)source, source ? strlen(source):0};return s;
    } abort();
}
static GLuint pair(const char *v,const char *f) {
    GLuint p=glCreateProgram();
    glAttachShader(p,shader(GL_VERTEX_SHADER,v));glAttachShader(p,shader(GL_FRAGMENT_SHADER,f));return p;
}
static void cleanup(GLuint p) {
    for(unsigned i=0;i<2;++i) { unsigned s=pr[p].shaders[i];sh[s].dirty=1;unref(s); }
    pr[p].live=0;
}
static void reset(void) {
    for(unsigned i=0;i<shader_pair_count;++i) free(shader_pairs[i].source);
    memset(shader_pairs,0,sizeof(shader_pairs));memset(pr,0,sizeof(pr));memset(sh,0,sizeof(sh));
    shader_pair_count=shader_pair_source_bytes=shader_pair_binary_bytes=0;
    fail_alloc=fail_program=fail_link=oversized=0;
    pvz2_pair_hits=pvz2_pair_misses=pvz2_pair_bypasses=compiles=links=0;
}
int main(void) {
    GLuint p=pair("ab","c");pr[p].attribute=5;shader_pairs_link(p);
    assert(compiles==2 && shader_pair_count==1);
    unsigned oldv=pr[p].shaders[0],oldf=pr[p].shaders[1];cleanup(p);
    assert(sh[oldv].live && sh[oldf].live && sh[oldv].refs==1);
    p=pair("ab","c"); unsigned unused=pr[p].shaders[0];sh[unused].dirty=1;pr[p].attribute=9;
    shader_pairs_link(p);assert(compiles==2 && pvz2_pair_hits==1 && pr[p].attribute==9 && !sh[unused].live);
    assert(pr[p].shaders[0]==oldv && sh[oldv].refs==2);
    shader_pairs_link(p);assert(pvz2_pair_hits==1);cleanup(p);
    p=pair("a","bc");shader_pairs_link(p);assert(compiles==4 && pvz2_pair_hits==1);cleanup(p);
    p=pair("ab","different");shader_pairs_link(p);assert(compiles==6);cleanup(p);
    shader_pairs_source_changed(oldv);
    p=pair("ab","c");shader_pairs_link(p);assert(compiles==8 && pvz2_pair_hits==1);cleanup(p);
    p=pair(NULL,"c");shader_pairs_link(p);assert(pvz2_pair_bypasses==1);cleanup(p);
    reset();
    char names[32][16];
    for(unsigned i=0;i<32;++i) { snprintf(names[i],16,"vertex%u",i);p=pair(names[i],"frag");shader_pairs_link(p);cleanup(p); }
    assert(shader_pair_count==16 && compiles==64);
    p=pair(names[0],"frag");shader_pairs_link(p);assert(compiles==64 && pvz2_pair_hits==1);cleanup(p);
    for(unsigned failure=0;failure<5;++failure) {
        reset();p=pair("v","f");
        if(failure==0)fail_alloc=1;if(failure==1)fail_program=~0u;
        if(failure==2)fail_link=1;if(failure==3)oversized=1;
        if(failure==4)shader_pair_source_bytes=SHADER_PAIR_SOURCE_BUDGET;
        shader_pairs_link(p);assert(!shader_pair_count && !pvz2_pair_hits);
        assert(pr[p].linked == (failure!=2));cleanup(p);
    }
    reset();
    puts("PASS: exact pair/stage boundaries, reuse without compilation, per-program attributes, deleted-shader retention, ID reuse, source mutation, capacity, OOM and failed-link fallback");
}
'''
(w/'check.c').write_text(c)
subprocess.run(['gcc','-O2','-static','-I'+str(r/'vita/direct/source'),str(w/'check.c'),'-o',str(w/'check.exe')],check=True)
run=subprocess.run([str(w/'check.exe')],check=True,capture_output=True,text=True,timeout=5)
(w/'result.txt').write_text(run.stdout)
print(run.stdout.strip()); print(w)
