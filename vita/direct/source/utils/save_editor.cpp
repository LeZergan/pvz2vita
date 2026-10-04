/* Lossless startup editor for 4.5.2's unencrypted RTON PlayerInfo saves.
 * Parse structure/string references, then splice only c/g numeric tokens.
 * Never rewrite the document, reorder properties, or guess encrypted formats. */
#include "utils/save_editor.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <unistd.h>

namespace {
constexpr size_t limit = 8 * 1024 * 1024;
struct Field { size_t start = 0, end = 0; uint32_t value = 0; bool valid = false; };
struct Profile { std::string name; Field coins, gems; };
struct Value {
    std::string text;
    Field number;
    Field coins, gems;
    std::string name;
};
struct Parser {
    const std::vector<unsigned char> &b;
    size_t p = 8;
    bool ok = true;
    unsigned nodes = 0;
    std::vector<std::string> ascii, unicode;
    std::vector<Profile> profiles;
    unsigned byte() { if (p >= b.size()) { ok = false; return 0; } return b[p++]; }
    uint64_t fixed(unsigned n) {
        if (n > b.size() - p) { ok = false; return 0; }
        uint64_t v = 0; for (unsigned i = 0; i < n; ++i) v |= uint64_t(b[p++]) << (8 * i);
        return v;
    }
    uint64_t var() {
        uint64_t v = 0;
        for (unsigned i = 0; i < 10; ++i) {
            unsigned c = byte();
            if (!ok || (i == 9 && c > 1)) { ok = false; return 0; }
            v |= uint64_t(c & 127) << (7 * i);
            if (!(c & 128)) return v;
        }
        ok = false; return 0;
    }
    std::string string(unsigned tag) {
        if (tag == 0x91 || tag == 0x93) {
            uint64_t i = var(); auto &cache = tag == 0x91 ? ascii : unicode;
            if (i >= cache.size()) { ok = false; return {}; }
            return cache[size_t(i)];
        }
        if (tag != 0x81 && tag != 0x82 && tag != 0x90 && tag != 0x92) { ok = false; return {}; }
        if (tag == 0x82 || tag == 0x92) var(); // UTF-8 character count precedes byte count.
        uint64_t n = var();
        if (!ok || n > b.size() - p || n > limit) { ok = false; return {}; }
        std::string s(reinterpret_cast<const char *>(b.data() + p), size_t(n)); p += size_t(n);
        if (tag == 0x90) ascii.push_back(s);
        if (tag == 0x92) unicode.push_back(s);
        return s;
    }
    Value object(unsigned depth, bool player_data) {
        Value out, data;
        std::string cls;
        unsigned seen_c = 0, seen_g = 0, seen_name = 0, seen_class = 0, seen_data = 0;
        while (ok && p < b.size() && b[p] != 0xff) {
            std::string key = string(byte());
            Value v = value(depth + 1, key == "objdata");
            if (key == "objclass") { cls = v.text; if (++seen_class > 1) ok = false; }
            if (key == "objdata") { data = v; if (++seen_data > 1) ok = false; }
            if (player_data && key == "c") { out.coins = v.number; if (++seen_c > 1) ok = false; }
            if (player_data && key == "g") { out.gems = v.number; if (++seen_g > 1) ok = false; }
            if (player_data && key == "n") { out.name = v.text; if (++seen_name > 1) ok = false; }
        }
        if (byte() != 0xff) ok = false;
        if (cls == "PlayerInfo") {
            if (!data.coins.valid || !data.gems.valid || profiles.size() >= 16) ok = false;
            else profiles.push_back({data.name, data.coins, data.gems});
        }
        return out;
    }
    Value value(unsigned depth, bool player_data = false) {
        Value out;
        if (!ok || depth > 64 || ++nodes > 500000) { ok = false; return out; }
        size_t start = p; unsigned tag = byte();
        if (tag == 0x85) return object(depth, player_data);
        if (tag == 0x86) {
            if (byte() != 0xfd) { ok = false; return out; }
            uint64_t n = var();
            if (n > 500000 || n > b.size() - p) { ok = false; return out; }
            for (uint64_t i = 0; ok && i < n; ++i) value(depth + 1);
            if (byte() != 0xfe) ok = false;
            return out;
        }
        if (tag >= 0x81 && (tag == 0x81 || tag == 0x82 || (tag >= 0x90 && tag <= 0x93))) {
            out.text = string(tag); return out;
        }
        if (tag == 0 || tag == 1 || tag == 0x84) return out;
        // Resource IDs are skipped structurally; they are never editable.
        if (tag == 0x83) {
            unsigned subtype = byte();
            if (subtype == 0) return out;
            if (subtype == 2) { var(); uint64_t n = var(); if (n > b.size()-p) ok=false; else p+=size_t(n); var(); var(); fixed(4); return out; }
            if (subtype == 3) {
                for (int i=0;i<2;++i) { var(); uint64_t n=var(); if(n>b.size()-p) {ok=false;break;} p+=size_t(n); }
                return out;
            }
            ok = false; return out;
        }
        uint64_t n = 0; bool number = true, negative = false;
        switch (tag) {
        case 0x08: n=fixed(1); negative=(n&0x80)!=0; break;
        case 0x0a: n=fixed(1); break;
        case 0x10: n=fixed(2); negative=(n&0x8000)!=0; break;
        case 0x12: n=fixed(2); break;
        case 0x20: n=fixed(4); negative=(n&0x80000000u)!=0; break;
        case 0x26: n=fixed(4); break;
        case 0x40: n=fixed(8); negative=(n>>63)!=0; break;
        case 0x46: n=fixed(8); break;
        case 0x24: case 0x28: case 0x44: case 0x48: n=var(); break;
        case 0x25: case 0x29: case 0x45: case 0x49: n=var(); negative=(n&1)!=0; n>>=1; break;
        case 0x09: case 0x0b: case 0x11: case 0x13: case 0x21: case 0x27: case 0x41: case 0x47: break;
        case 0x22: fixed(4); number=false; break;
        case 0x42: fixed(8); number=false; break;
        case 0x23: case 0x43: number=false; break;
        default: ok=false; number=false; break;
        }
        out.number = {start, p, uint32_t(n), ok && number && !negative && n <= 0x7fffffffu};
        return out;
    }
    bool parse() {
        if (b.size() < 13 || std::memcmp(b.data(), "RTON\x01\0\0\0", 8)) return false;
        object(0, false);
        return ok && p + 4 == b.size() && !std::memcmp(b.data()+p, "DONE", 4) && !profiles.empty();
    }
};
bool load(const char *path, std::vector<unsigned char> &b) {
    FILE *f = std::fopen(path, "rb"); if (!f) return false;
    bool ok = !std::fseek(f, 0, SEEK_END); long n = ok ? std::ftell(f) : -1;
    ok = n >= 0 && size_t(n) <= limit;
    if (ok) { b.resize(size_t(n)); std::rewind(f); ok=std::fread(b.data(),1,b.size(),f)==b.size(); }
    std::fclose(f); return ok;
}
bool exists(const std::string &path) { FILE *f=std::fopen(path.c_str(),"rb"); if(!f)return false;std::fclose(f);return true; }
bool write(const std::string &path, const std::vector<unsigned char> &b) {
    FILE *f = std::fopen(path.c_str(), "wb"); if (!f) return false;
    bool ok = std::fwrite(b.data(),1,b.size(),f)==b.size() && !std::fflush(f);
    if (ok && fsync(fileno(f))) ok = false;
    if (std::fclose(f)) ok=false;
    std::vector<unsigned char> check;
    return ok && load(path.c_str(),check) && check==b;
}
int fail(char *error, size_t capacity, const char *text) { if(error&&capacity)std::snprintf(error,capacity,"%s",text);return 0; }
}

extern "C" int pvz2_save_recover(const char *path, char *error, size_t capacity) {
    std::string old = std::string(path)+".editor-old";
    if (!exists(path) && exists(old) && std::rename(old.c_str(),path))
        return fail(error,capacity,"Save recovery failed. Keep pp.dat.editor-old and check free space.");
    return 1;
}
extern "C" int pvz2_save_read(const char *path, Pvz2SaveProfile *out, unsigned cap, char *error, size_t ec) {
    if (!out || !cap) return fail(error,ec,"No profile buffer.");
    std::vector<unsigned char> b;
    if (!load(path,b)) return fail(error,ec,"No readable save. Create a player in the game first.");
    Parser parser{b};
    if (!parser.parse()) return fail(error,ec,"Unsupported/damaged save. Only unencrypted 4.5.2 PlayerInfo RTON is editable.");
    if (parser.profiles.size()>cap) return fail(error,ec,"Too many profiles for the editor.");
    for (unsigned i=0;i<parser.profiles.size();++i) {
        const auto &p=parser.profiles[i]; std::snprintf(out[i].name,sizeof(out[i].name),"%s",p.name.c_str());
        out[i].coins=p.coins.value;out[i].gems=p.gems.value;
    }
    return int(parser.profiles.size());
}
extern "C" int pvz2_save_apply(const char *path, unsigned profile, uint32_t coins, uint32_t gems, char *error, size_t ec) {
    if(profile>=16)return fail(error,ec,"Unsupported save/profile. Nothing changed.");
    Pvz2SaveProfile desired[16] = {};
    desired[profile].coins=coins;desired[profile].gems=gems;
    return pvz2_save_apply_batch(path,desired,profile+1,1u<<profile,error,ec);
}
extern "C" int pvz2_save_apply_batch(const char *path, const Pvz2SaveProfile *profiles, unsigned count,
                                      uint32_t selected_mask, char *error, size_t ec) {
    if(!profiles || !count || count>16 || (selected_mask>>count))
        return fail(error,ec,"Unsupported save/profile. Nothing changed.");
    if(!selected_mask)return 1;
    std::vector<unsigned char> original;
    if (!load(path,original)) return fail(error,ec,"Cannot read save.");
    Parser parser{original};
    if (!parser.parse() || count>parser.profiles.size()) return fail(error,ec,"Unsupported save/profile. Nothing changed.");
    struct Edit {Field field;uint32_t value;};
    std::vector<Edit> edits;
    for(unsigned i=0;i<count;++i)if(selected_mask&(1u<<i)) {
        const auto &p=parser.profiles[i];
        const auto &desired=profiles[i];
        if(desired.coins==p.coins.value && desired.gems==p.gems.value)continue;
        if((desired.coins!=p.coins.value && desired.coins>999999999u) ||
           (desired.gems!=p.gems.value && desired.gems>999999999u))
            return fail(error,ec,"Values must be between 0 and 999999999.");
        if(desired.coins!=p.coins.value)edits.push_back({p.coins,desired.coins});
        if(desired.gems!=p.gems.value)edits.push_back({p.gems,desired.gems});
    }
    if(edits.empty())return 1;
    std::sort(edits.begin(),edits.end(),[](const Edit &a,const Edit &b){return a.field.start<b.field.start;});
    std::vector<unsigned char> changed; size_t pos=0;
    for(const auto &e:edits) {
        changed.insert(changed.end(),original.begin()+pos,original.begin()+e.field.start);
        // Positive signed int32, accepted by the same native integer reader.
        changed.push_back(0x20);
        for(unsigned i=0;i<4;++i)changed.push_back((e.value>>(8*i))&255);
        pos=e.field.end;
    }
    changed.insert(changed.end(),original.begin()+pos,original.end());
    Parser check{changed};
    if(!check.parse() || check.profiles.size()!=parser.profiles.size())
        return fail(error,ec,"Edited save did not validate. Nothing changed.");
    for(unsigned i=0;i<parser.profiles.size();++i) {
        uint32_t coins=(selected_mask&(1u<<i))?profiles[i].coins:parser.profiles[i].coins.value;
        uint32_t gems=(selected_mask&(1u<<i))?profiles[i].gems:parser.profiles[i].gems.value;
        if(check.profiles[i].coins.value!=coins || check.profiles[i].gems.value!=gems)
            return fail(error,ec,"Edited save did not validate. Nothing changed.");
    }
    std::string backup;
    for(unsigned i=0;i<1000;++i) {
        backup=std::string(path)+".backup-"+std::to_string(i);
        if(!exists(backup))break;
        if(i==999)return fail(error,ec,"Backup slots full. Archive old pp.dat.backup-* files first.");
    }
    std::string temp=std::string(path)+".editor-tmp", old=std::string(path)+".editor-old";
    if(!write(backup,original))return fail(error,ec,"Backup failed. Original save preserved.");
    if(!write(temp,changed)) {std::remove(temp.c_str());return fail(error,ec,"Edited save write failed. Original preserved.");}
    std::vector<unsigned char> current;
    if(!load(path,current) || current!=original) {std::remove(temp.c_str());return fail(error,ec,"Save changed while editing. Nothing applied.");}
    // Keep a recoverable original across the two renames, including power loss.
    if(exists(old) && std::remove(old.c_str()))return fail(error,ec,"Cannot clear previous transaction. Backup preserved.");
    if(std::rename(path,old.c_str()))return fail(error,ec,"Cannot stage original save. Backup preserved.");
    if(std::rename(temp.c_str(),path)) {
        std::rename(old.c_str(),path);
        return fail(error,ec,"Cannot commit edited save. Relaunch to recover original.");
    }
    std::remove(old.c_str());
    if(error&&ec)std::snprintf(error,ec,"Saved. Original backup: pp.dat.backup-%s",backup.c_str()+backup.find_last_of('-')+1);
    return 1;
}
