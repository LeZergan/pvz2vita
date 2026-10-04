"""Actual scalar/vector wrappers and cache with GL executable metadata mocked."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1];w=Path(tempfile.mkdtemp(prefix='sprite-uniforms-',dir=r/'out'))
s=(r/'vita/direct/source/utils/glutil.c').read_text(encoding="utf-8")
def part(a,b):return s[s.index(a):s.index(b)]
c=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
typedef unsigned GLuint,GLenum;
typedef int GLint,GLsizei;
typedef float GLfloat;
#define GL_CURRENT_PROGRAM 1
#define GL_LINK_STATUS 2
#define GL_ACTIVE_UNIFORMS 3
#define GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS 4
#define GL_INT 5
#define GL_SAMPLER_2D 6
#define GL_SAMPLER_CUBE 7
#define GL_FLOAT_VEC4 8
#define GL_TRUE 1
#define MCSM_FAST_FINAL_RUNTIME 1
#define l_info(...) ((void)0)
static int current=1,linked=1,array,metadata_queries;
static unsigned calls_i,calls_f,invalid,split;
static float colors[1025][4];static int sampler[1025];
static void glGetIntegerv(int q,int *v){*v=q==GL_CURRENT_PROGRAM?current:8;}
static void glGetProgramiv(unsigned p,int q,int *v){++metadata_queries;*v=q==GL_LINK_STATUS?linked:3;}
static void glGetActiveUniform(unsigned p,unsigned i,int cap,int *length,int *size,unsigned *type,char *name){
 *length=1;*size=array && i==1?2:1;*type=i==0?GL_SAMPLER_2D:i==1?GL_FLOAT_VEC4:999;
 name[0]='a'+i;name[1]=0;
}
static int glGetUniformLocation(unsigned p,const char *n){return n[0]-'a'+10;}
static void glUniform1i(int l,int v){++calls_i;if(current<=0||l!=10||v<0||v>=8){++invalid;return;}sampler[current]=v;}
static void glUniform1iv(int l,int n,const int *v){glUniform1i(l,*v);}
static void glUniform4f(int l,float a,float b,float c,float d){++calls_f;float v[]={a,b,c,d};if(current<=0||l!=11){++invalid;return;}memcpy(colors[current],v,16);}
static void glUniform4fv(int l,int n,const float *v){glUniform4f(l,v[0],v[1],v[2],v[3]);}
static void mcsm_log_anim_pose(int l,int c,const float *v){}
static int gl_uniform4fv_split_telltale(int l,int c,const float *v){++split;return 0;}
#include "utils/sprite_uniforms.h"
'''
c+=part('static int uniform_scalar_should_skip(','void glUniform1f_soloader(')
c+=part('void glUniform1i_soloader(','void glUniform2f_soloader(')
c+=part('void glUniform4f_soloader(','/* MEGA-LOG:')
c+=part('void glUniform4fv_soloader(','void glUniform4i_soloader(')
c+=r'''
int main(void){
 float white[]={1,1,1,1},red[]={1,0,0,1};int zero=0;
 glUniform1i_soloader(10,0);glUniform4f_soloader(11,1,1,1,1);
 int queries=metadata_queries;
 for(int i=0;i<10000;++i){glUniform1iv_soloader(10,1,&zero);glUniform4fv_soloader(11,1,white);}
 assert(calls_i==1&&calls_f==1&&sprite_uniform_skipped==20000&&metadata_queries==queries);
 glUniform4fv_soloader(11,1,red);assert(calls_f==2&&colors[1][1]==0);
 glUniform4f_soloader(11,1,1,1,1);assert(calls_f==3&&colors[1][1]==1);
 glUniform1i_soloader(10,2);assert(calls_i==2&&sampler[1]==2);
 current=2;glUniform1i_soloader(10,0);glUniform4fv_soloader(11,1,white);assert(calls_i==3&&calls_f==4);
 current=1;glUniform1i_soloader(10,2);assert(calls_i==3);
 sprite_uniforms_begin();glUniform1i_soloader(10,2);assert(calls_i==4);
 sprite_uniforms_invalidate(1);glUniform4fv_soloader(11,1,white);assert(calls_f==5);
 glUniform4fv_soloader(11,2,white);glUniform4fv_soloader(11,1,white);assert(calls_f==7);
 sprite_uniforms_invalidate(1);array=1;glUniform4fv_soloader(11,1,white);glUniform4fv_soloader(11,1,white);assert(calls_f==9);array=0;
 glUniform1i_soloader(10,-1);glUniform1i_soloader(10,-1);assert(invalid==2);
 glUniform1i_soloader(10,8);glUniform1i_soloader(10,8);assert(invalid==4);
 glUniform4f_soloader(500,1,1,1,1);glUniform4f_soloader(500,1,1,1,1);assert(invalid==6);
 glUniform4f_soloader(12,1,1,1,1);glUniform4f_soloader(12,1,1,1,1);assert(invalid==8);
 sprite_uniforms_invalidate(1);float bad[]={NAN,1,1,1};unsigned n=calls_f;
 glUniform4fv_soloader(11,1,bad);glUniform4fv_soloader(11,1,bad);assert(calls_f==n+2);
 // A noncached setter at the same location may mutate a permissive driver.
 sprite_uniforms_invalidate(1);glUniform4fv_soloader(11,1,white);
 sprite_uniforms_other(11,1);memcpy(colors[1],red,16);n=calls_f;
 glUniform4fv_soloader(11,1,white);assert(calls_f==n+1&&colors[1][1]==1);
 current=0;n=calls_i;glUniform1i_soloader(10,0);glUniform1i_soloader(10,0);assert(calls_i==n+2);
 // Program IDs 1 and 129 collide in the old direct-mapped cache. Warm both,
 // then alternating them must neither enumerate metadata nor re-upload values.
 current=1;sprite_uniforms_invalidate(1);glUniform1i_soloader(10,0);
 current=129;sprite_uniforms_invalidate(129);glUniform1i_soloader(10,2);
 queries=metadata_queries;n=calls_i;
 for(int i=0;i<10000;++i){current=1;glUniform1i_soloader(10,0);current=129;glUniform1i_soloader(10,2);}
 assert(metadata_queries==queries&&calls_i==n&&sampler[1]==0&&sampler[129]==2);
 // Collision eviction remains bounded and correctly forwards the replacement.
 for(unsigned p=257;p<=641;p+=128){current=p;glUniform1i_soloader(10,3);assert(sampler[p]==3);}
 current=1;glUniform1i_soloader(10,0);assert(sampler[1]==0);
 sprite_uniforms_other(10,1);n=calls_i;glUniform1i_soloader(10,0);assert(calls_i==n+1);
 glUniform4f_soloader(10,1,1,1,1);n=calls_i;
 glUniform1i_soloader(10,0);assert(calls_i==n+1); // Wrong-kind aliases cannot retain stale values.
 assert(strstr("", "x")==NULL);
 puts("PASS: 40000 redundant uploads suppressed, including colliding programs with no repeated metadata queries; bounded eviction; changed values/programs and direct bindings; aliases; frame/relink/delete invalidation; arrays, types, invalid samplers, NaNs reach driver");
}
'''
(w/'test.c').write_text(c);exe=w/'test.exe'
subprocess.run(['gcc','-O2','-static','-I',str(r/'vita/direct/source'),str(w/'test.c'),'-o',str(exe)],check=True,timeout=30)
result=subprocess.run([str(exe)],check=True,capture_output=True,text=True,timeout=5)
(w/'result.txt').write_text(result.stdout);print(result.stdout.strip());print(w)
