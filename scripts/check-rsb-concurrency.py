"""Exercise production RSB lookup locking while extraction is deliberately stalled."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'vita/direct/source/reimpl/rsb_index_vita.cpp').read_text()
w=Path(tempfile.mkdtemp(prefix='rsb-concurrency-',dir=r/'out'))
code=r'''
#include <pthread.h>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstring>
#include <cctype>
'''
code+=s[s.index('constexpr char kAssetScheme'):s.index('/* The exact source archive')]
code+=r'''
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static bool entered,proceed;static unsigned loads,extractions;
static bool load_index(){
 ++loads;Entry plain;plain.offset=123;plain.size=17;g_entries.emplace("PLAIN",plain);
 Entry blocked;blocked.offset=50;blocked.size=7;blocked.block_size=100;blocked.within_block=13;g_entries.emplace("BLOCK",blocked);
 Entry texture;texture.compressed=true;g_entries.emplace("TEXTURE",texture);return true;
}
static const char *pvz2_obb_path(){return "game.obb";}
static bool cache_block(Entry&e){
 if(!e.cache_path.empty())return true;
 pthread_mutex_lock(&gate);entered=true;pthread_cond_broadcast(&changed);
 while(!proceed)pthread_cond_wait(&changed,&gate);
 ++extractions;e.cache_path="cache/rsb452/test.bin";pthread_mutex_unlock(&gate);return true;
}
'''
code+=s[s.index('static Entry *rsb_lookup_entry'):s.index('/* No index lock:')]
code+=r'''
static void *worker(void*){uint64_t off;uint32_t len;const char*p=vita_rsb_locate("block",&off,&len);assert(p&&off==13&&len==7);return nullptr;}
int main(){
 pthread_t thread;pthread_create(&thread,nullptr,worker,nullptr);
 pthread_mutex_lock(&gate);while(!entered)pthread_cond_wait(&changed,&gate);pthread_mutex_unlock(&gate);
 /* These calls must complete while the extractor is still blocked. */
 uint64_t off;uint32_t len;int compressed;
 for(int i=0;i<10000;i++){
  assert(vita_rsb_find("ASSET:plain",&off,&len,&compressed)&&off==123&&len==17&&!compressed);
  assert(!strcmp(vita_rsb_locate("plain",&off,&len),"game.obb")&&off==123&&len==17);
 }
 assert(!vita_rsb_locate("texture",nullptr,nullptr));
 assert(!vita_rsb_locate("missing",nullptr,nullptr)&&g_misses==1);
 pthread_mutex_lock(&gate);proceed=true;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&gate);
 pthread_join(thread,nullptr);assert(loads==1&&extractions==1);
 const char *saved=vita_rsb_locate("block",nullptr,nullptr);
 for(int i=0;i<10000;i++)assert(vita_rsb_locate("block",nullptr,nullptr)==saved);
 assert(extractions==1&&!strcmp(saved,"cache/rsb452/test.bin"));
 puts("PASS: 20000 unrelated lookups finish during blocked extraction; one index load, stable cached paths and optional outputs");
}
'''
(w/'check.cpp').write_text(code)
subprocess.run(['g++','-std=c++14','-O2','-static','-pthread',str(w/'check.cpp'),'-o',str(w/'check.exe')],check=True)
subprocess.run([str(w/'check.exe')],check=True,timeout=15)
print(w)
