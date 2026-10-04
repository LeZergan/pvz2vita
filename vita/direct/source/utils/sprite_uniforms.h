/* Bounded cache for redundant sprite color/sampler uploads. Render-thread only.
 * Metadata is validated against the real linked executable, not guessed from
 * location numbers. Begin-frame clears values after loader-owned rendering. */
#ifndef PVZ2_SPRITE_UNIFORMS_H
#define PVZ2_SPRITE_UNIFORMS_H
#include <string.h>
#include <stdint.h>
#ifndef GL_BOOL
#define GL_BOOL 0x8B56
#endif
typedef struct {
    GLuint program;
    GLint location;
    unsigned kind, eligible, have, sampler_limit;
    unsigned char bytes[16];
} SpriteUniform;
static SpriteUniform sprite_uniforms[128];
static unsigned sprite_uniform_victim[32];
static unsigned sprite_uniform_skipped;
/* Four entries per bucket retain linked metadata when unrelated programs
 * hash alike. Direct mapping caused full uniform enumeration every switch. */
static SpriteUniform *sprite_uniform_entry(GLuint program, GLint location) {
    unsigned bucket=(program*31u ^ (unsigned)location)&31u;
    SpriteUniform *base=&sprite_uniforms[bucket*4],*empty=NULL;
    for(unsigned i=0;i<4;++i) {
        if(base[i].program==program && base[i].location==location)return &base[i];
        if(!base[i].program)empty=&base[i];
    }
    return empty?empty:&base[sprite_uniform_victim[bucket]++&3u];
}
static void sprite_uniforms_begin(void) {
    for(unsigned i=0;i<128;++i)sprite_uniforms[i].have=0;
}
static void sprite_uniforms_invalidate(GLuint program) {
    for(unsigned i=0;i<128;++i)
        if(sprite_uniforms[i].program==program)memset(&sprite_uniforms[i],0,sizeof(sprite_uniforms[i]));
}
static void sprite_uniforms_other(GLint location, GLsizei count) {
    if(count != 1) { sprite_uniforms_begin(); return; }
    GLint current=0;
    glGetIntegerv(GL_CURRENT_PROGRAM,&current);
    unsigned bucket=((unsigned)current*31u ^ (unsigned)location)&31u;
    for(unsigned i=0;i<4;++i) {
        SpriteUniform *entry=&sprite_uniforms[bucket*4+i];
        if(entry->program==(GLuint)current && entry->location==location)entry->have=0;
    }
}
static int sprite_uniform_same(GLint location, unsigned kind, const void *value) {
    GLint current=0;
    glGetIntegerv(GL_CURRENT_PROGRAM,&current);
    if(current<=0 || current>1024 || location<0)return 0;
    SpriteUniform *entry=sprite_uniform_entry(current,location);
    if(entry->program!=(GLuint)current || entry->location!=location || entry->kind!=kind) {
        memset(entry,0,sizeof(*entry));entry->program=current;entry->location=location;entry->kind=kind;
        GLint linked=0,count=0;
        glGetProgramiv(current,GL_LINK_STATUS,&linked);
        if(linked!=GL_TRUE)return 0;
        glGetProgramiv(current,GL_ACTIVE_UNIFORMS,&count);
        if(count<0 || count>192)return 0;
        for(GLint i=0;i<count;++i) {
            char name[128]={0};GLsizei length=0;GLint size=0;GLenum type=0;
            glGetActiveUniform(current,i,sizeof(name),&length,&size,&type,name);
            if(length<=0 || length>=127 || size!=1 || glGetUniformLocation(current,name)!=location)continue;
            if(kind==1 && (type==GL_INT || type==GL_BOOL || type==GL_SAMPLER_2D || type==GL_SAMPLER_CUBE)) {
                entry->eligible=1;
                if(type==GL_SAMPLER_2D || type==GL_SAMPLER_CUBE) {
                    GLint units=0;glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,&units);
                    entry->sampler_limit=units>0?(unsigned)units:0;
                    if(!entry->sampler_limit)entry->eligible=0;
                }
            }
            if(kind==4 && type==GL_FLOAT_VEC4)entry->eligible=1;
            break;
        }
    }
    if(!entry->eligible)return 0;
    if(kind==1 && entry->sampler_limit && (*(const GLint*)value<0 || (unsigned)*(const GLint*)value>=entry->sampler_limit)) {
        entry->have=0;return 0;
    }
    if(kind==4) {
        const GLfloat *f=value;
        for(unsigned i=0;i<4;++i)if(f[i]!=f[i] || f[i]>3.0e38f || f[i]<-3.0e38f){entry->have=0;return 0;}
    }
    size_t size=kind==1?sizeof(GLint):4*sizeof(GLfloat);
    if(entry->have && !memcmp(entry->bytes,value,size)){++sprite_uniform_skipped;return 1;}
    memcpy(entry->bytes,value,size);entry->have=1;return 0;
}
#endif
