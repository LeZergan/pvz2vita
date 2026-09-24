/* Vita adaptation of PvZ2Native's runtime/rsb_index.cpp.
 *
 * Keep this parser structurally identical to upstream: Android's
 * Resources_GetAssetFileInfo returns a container path plus byte range, and
 * for PvZ2 4.5.2 that container is the main RSB stored as the expansion OBB.
 */

#include "reimpl/rsb_index_vita.h"
#include "utils/logger.h"
#include "utils/cache_crc.h"
#include "utils/boot_check.h"
#include "utils/telemetry.h"
#include "reimpl/pthr.h"
#include <psp2/kernel/processmgr.h>
#include "reimpl/miniz_zlib.h"
#include <psp2/io/stat.h>

#include <pthread.h>

#include <cctype>
#include <cstdio>
#include <sys/stat.h>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <atomic>

namespace {
#ifndef PVZ2_INDEX_PATH
#define PVZ2_INDEX_PATH "app0:rsb452.idx"
#endif

constexpr char kAssetScheme[] = "ASSET:";
constexpr size_t kAssetSchemeLen = sizeof(kAssetScheme) - 1;


struct Entry {
    uint64_t offset = 0;
    uint32_t size = 0;
    bool compressed = false;
    uint32_t block_offset = 0;
    uint32_t block_size = 0;
    uint32_t unpacked_size = 0;
    uint32_t within_block = 0;
    std::string cache_path;
};

pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t g_cache_lock = PTHREAD_MUTEX_INITIALIZER;
bool g_tried = false;
bool g_loaded = false;
std::unordered_map<std::string, Entry> g_entries;
std::atomic<unsigned> g_queries{0}, g_misses{0}, g_cache_failures{0}, g_blocks_ready{0};
std::atomic<unsigned> g_active_block{0}, g_written{0}, g_expected{0};

uint32_t rd32(const std::vector<uint8_t> &bytes, size_t offset) {
    if (offset + 4 > bytes.size()) return 0;
    uint32_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

uint32_t rd24(const std::vector<uint8_t> &bytes, size_t offset) {
    if (offset + 3 > bytes.size()) return 0;
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16);
}

std::string normalize(const char *guest_path) {
    std::string name = guest_path ? guest_path : "";
    if (name.compare(0, kAssetSchemeLen, kAssetScheme) == 0)
        name.erase(0, kAssetSchemeLen);
    for (char &ch : name) {
        if (ch == '/') ch = '\\';
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return name;
}

/* The exact source archive is verified by the build script before packaging.
 * A small sequential metadata read replaces thousands of random OBB reads.
 * Never publish a partial or corrupt index; fall back to the archive parser. */
bool load_bundled_index(uint64_t file_size, const std::vector<uint8_t> &head) {
    FILE *file = std::fopen(PVZ2_INDEX_PATH, "rb");
    if (!file) return false;
    uint32_t header[8] = {};
    bool ok = std::fread(header, sizeof(header), 1, file) == 1 &&
        header[0] == 0x32495a50u && header[1] == 1 && header[2] == file_size &&
        header[3] == pvz2_cache_crc32(0, head.data(), head.size()) && header[7] == 0 &&
        header[4] > 0 && header[4] <= 16384 && header[5] <= 2 * 1024 * 1024 &&
        header[5] >= header[4] * 37u;
    std::vector<uint8_t> bytes(ok ? header[5] : 0);
    if (ok) ok = std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size() &&
                 std::fgetc(file) == EOF && pvz2_cache_crc32(0, bytes.data(), bytes.size()) == header[6];
    std::fclose(file);
    if (!ok) return false;
    std::unordered_map<std::string, Entry> parsed;
    parsed.reserve(header[4]);
    size_t pos = 0;
    for (uint32_t i = 0; i < header[4]; ++i) {
        if (bytes.size() - pos < 36) return false;
        Entry e;
        e.offset = rd32(bytes, pos) | (uint64_t(rd32(bytes, pos + 4)) << 32);
        e.size = rd32(bytes, pos + 8);
        uint32_t flags = rd32(bytes, pos + 12);
        e.compressed = flags != 0;
        e.block_offset = rd32(bytes, pos + 16);
        e.block_size = rd32(bytes, pos + 20);
        e.unpacked_size = rd32(bytes, pos + 24);
        e.within_block = rd32(bytes, pos + 28);
        uint32_t n = rd32(bytes, pos + 32);
        pos += 36;
        if (flags > 1 || n == 0 || n > 1024 || n > bytes.size() - pos ||
            std::memchr(bytes.data() + pos, 0, n)) return false;
        if (e.block_size) {
            if (uint64_t(e.block_offset) + e.block_size > file_size ||
                e.within_block > e.unpacked_size || e.size > e.unpacked_size - e.within_block)
                return false;
        } else if (!e.compressed && (e.offset > file_size || e.size > file_size - e.offset))
            return false;
        std::string name(reinterpret_cast<char *>(bytes.data() + pos), n);
        if (!parsed.emplace(std::move(name), e).second) return false;
        pos += n;
    }
    if (pos != bytes.size()) return false;
    g_entries.swap(parsed);
    telemetry_log("ASSETS", "bundled index: %u entries, %u bytes; skipped full RSG scan",
                  header[4], header[5]);
    return true;
}

bool load_index() {
    g_entries.clear();

    FILE *file = std::fopen(pvz2_obb_path(), "rb");
    if (!file) {
        l_error("[rsb-index] cannot open %s", pvz2_obb_path());
        return false;
    }

    uint64_t file_size = 0;
    if (std::fseek(file, 0, SEEK_END) == 0) {
        const long end = std::ftell(file);
        if (end > 0) file_size = static_cast<uint64_t>(end);
    }
    std::rewind(file);

    std::vector<uint8_t> head(0x100);
    if (std::fread(head.data(), 1, head.size(), file) != head.size() ||
        std::memcmp(head.data(), "1bsr", 4) != 0) {
        l_error("[rsb-index] %s is not an RSB", pvz2_obb_path());
        std::fclose(file);
        return false;
    }

    if (load_bundled_index(file_size, head)) {
        std::fclose(file);
        return true;
    }
    telemetry_log("ASSETS", "bundled index unavailable/invalid; scanning original OBB");
    const uint32_t rsg_number = rd32(head, 0x28);
    const uint32_t rsg_info_begin = rd32(head, 0x2c);
    const uint32_t rsg_info_each = rd32(head, 0x30);
    if (rsg_number == 0 || rsg_number > 16384 || rsg_info_each < 0x88 ||
        uint64_t(rsg_number) * rsg_info_each > 4 * 1024 * 1024 ||
        uint64_t(rsg_info_begin) + uint64_t(rsg_number) * rsg_info_each > file_size) {
        l_error("[rsb-index] implausible RSG table n=%u each=%u",
                rsg_number, rsg_info_each);
        std::fclose(file);
        return false;
    }

    std::vector<uint8_t> info(static_cast<size_t>(rsg_number) * rsg_info_each);
    if (std::fseek(file, static_cast<long>(rsg_info_begin), SEEK_SET) != 0 ||
        std::fread(info.data(), 1, info.size(), file) != info.size()) {
        l_error("[rsb-index] truncated RSG info table");
        std::fclose(file);
        return false;
    }

    uint32_t skipped = 0;
    uint32_t obscured = 0;
    std::vector<uint8_t> rsg;
    for (uint32_t i = 0; i < rsg_number; ++i) {
        const size_t record = static_cast<size_t>(i) * rsg_info_each;
        const uint32_t rsg_offset = rd32(info, record + 0x80);
        if (rsg_offset == 0) continue;

        std::vector<uint8_t> header(0x60);
        if (std::fseek(file, static_cast<long>(rsg_offset), SEEK_SET) != 0 ||
            std::fread(header.data(), 1, header.size(), file) != header.size()) {
            ++skipped;
            continue;
        }
        if (std::memcmp(header.data(), "pgsr", 4) != 0) ++obscured;

        const uint32_t data_offset = rd32(header, 0x18);
        const bool compressed_data = (rd32(header, 0x10) & 2U) != 0;
        const uint32_t list_length = rd32(header, 0x48);
        const uint32_t list_begin = rd32(header, 0x4c);
        if (list_length == 0 ||
            static_cast<uint64_t>(rsg_offset) + list_begin + list_length > file_size) {
            ++skipped;
            continue;
        }

        rsg.assign(list_length, 0);
        if (std::fseek(file, static_cast<long>(rsg_offset + list_begin), SEEK_SET) != 0 ||
            std::fread(rsg.data(), 1, rsg.size(), file) != rsg.size()) {
            ++skipped;
            continue;
        }

        std::vector<std::pair<std::string, uint32_t>> stack;
        std::string name;
        uint32_t position = 0;
        while (position + 4 <= list_length) {
            const uint8_t ch = rsg[position];
            const uint32_t branch = rd24(rsg, position + 1);
            if (ch == 0) {
                if (position + 16 > list_length) break;
                const uint32_t flag = rd32(rsg, position + 4);
                const uint32_t offset = rd32(rsg, position + 8);
                const uint32_t size = rd32(rsg, position + 12);
                if (!name.empty()) {
                    Entry entry;
                    entry.offset = static_cast<uint64_t>(rsg_offset) + data_offset + offset;
                    entry.size = size;
                    entry.compressed = flag == 1;
                    if (compressed_data && flag == 0) {
                        entry.block_offset = rsg_offset + data_offset;
                        entry.block_size = rd32(header, 0x1c);
                        entry.unpacked_size = rd32(header, 0x20);
                        entry.within_block = offset;
                    }
                    g_entries.emplace(name, entry);
                }
                position += 16 + (flag == 1 ? 20 : 0);
                if (stack.empty()) {
                    if (name.empty()) break;
                    name.clear();
                    continue;
                }
                name = stack.back().first;
                position = stack.back().second;
                stack.pop_back();
            } else {
                if (branch != 0) stack.emplace_back(name, branch * 4);
                name.push_back(static_cast<char>(ch));
                position += 4;
            }
        }
    }
    std::fclose(file);

    const bool loaded = !g_entries.empty();
    l_info("[rsb-index] %u files indexed from %u RSGs (%u skipped, %u obscured)",
           static_cast<unsigned>(g_entries.size()), rsg_number, skipped, obscured);
    if (!loaded) l_error("[rsb-index] no files indexed");
    return loaded;
}

} // namespace

/* g_cache_lock is held by the caller. Check both source and cached output before
 * reusing a prior run's block; interrupted/corrupt/stale caches are rebuilt. */
static std::unordered_map<uint32_t, std::string> g_block_cache;
static bool crc_range(const char *path, uint32_t offset, uint32_t length,
                       uint32_t expected_crc, bool exact_file_size) {
    FILE *file = std::fopen(path, "rb");
    if (!file) return false;
    bool ok = true;
    if (exact_file_size)
        ok = std::fseek(file, 0, SEEK_END) == 0 && std::ftell(file) == long(length);
    ok = ok && std::fseek(file, long(offset), SEEK_SET) == 0;
    unsigned char bytes[16384];
    uint32_t crc = 0;
    while (ok && length) {
        size_t n = length < sizeof(bytes) ? length : sizeof(bytes);
        if (std::fread(bytes, 1, n, file) != n) { ok = false; break; }
        crc = pvz2_cache_crc32(crc, bytes, n);
        length -= n;
    }
    std::fclose(file);
    return ok && crc == expected_crc;
}
static bool cached_block_valid(const char *path, const Entry &entry) {
    const std::string meta = std::string(path) + ".meta";
    FILE *file = std::fopen(meta.c_str(), "rb");
    if (!file) return false;
    uint32_t h[6] = {};
    bool ok = std::fread(h, sizeof(h), 1, file) == 1 && std::fgetc(file) == EOF &&
        h[0] == 0x32434250u && h[1] == 1 && h[4] == entry.block_size && h[5] == entry.unpacked_size;
    std::fclose(file);
    return ok && crc_range(pvz2_obb_path(), entry.block_offset, entry.block_size, h[2], false) &&
                 crc_range(path, 0, entry.unpacked_size, h[3], true);
}
static bool cache_block(Entry &entry) {
    /* Published paths never change, so JNI callers can safely copy c_str()
     * after the cache lock has been released. */
    if (!entry.cache_path.empty()) return true;
    auto cached = g_block_cache.find(entry.block_offset);
    if (cached != g_block_cache.end()) {
        entry.cache_path = cached->second;
        return true;
    }
    if (!entry.block_size || !entry.unpacked_size ||
        entry.within_block > entry.unpacked_size ||
        entry.size > entry.unpacked_size - entry.within_block) return false;
    sceIoMkdir(DATA_PATH "cache", 0777);
    sceIoMkdir(DATA_PATH "cache/rsb452", 0777);
    char path[256];
    std::snprintf(path, sizeof(path), DATA_PATH "cache/rsb452/%08x.bin", entry.block_offset);
    if (cached_block_valid(path, entry)) {
        entry.cache_path = path;
        g_block_cache.emplace(entry.block_offset, entry.cache_path);
        static unsigned reuse_reports;
        if (reuse_reports++ < 4)
            telemetry_log("ASSETS", "reused verified resource block 0x%x (%u bytes)",
                          entry.block_offset, entry.unpacked_size);
        return true;
    }
    std::string partial = std::string(path) + ".tmp";
    g_active_block.store(entry.block_offset, std::memory_order_relaxed);
    g_written.store(0, std::memory_order_relaxed);
    g_expected.store(entry.unpacked_size, std::memory_order_relaxed);
    FILE *input = std::fopen(pvz2_obb_path(), "rb");
    FILE *output = std::fopen(partial.c_str(), "wb");
    if (!input || !output) {
        if (input) std::fclose(input);
        if (output) std::fclose(output);
        g_active_block.store(0, std::memory_order_relaxed);
        ++g_cache_failures;
        return false;
    }
    mz_stream stream = {};
    bool ok = std::fseek(input, static_cast<long>(entry.block_offset), SEEK_SET) == 0;
    const bool initialized = mz_inflateInit(&stream) == MZ_OK;
    ok = ok && initialized;
    std::vector<unsigned char> in(32768), out(32768);
    uint32_t remaining = entry.block_size, written = 0, source_crc = 0, output_crc = 0;
    int result = MZ_OK;
    while (ok && result != MZ_STREAM_END) {
        if (!stream.avail_in && remaining) {
            size_t n = remaining < in.size() ? remaining : in.size();
            if (std::fread(in.data(), 1, n, input) != n) { ok = false; break; }
            source_crc = pvz2_cache_crc32(source_crc, in.data(), n);
            remaining -= static_cast<uint32_t>(n);
            stream.next_in = in.data();
            stream.avail_in = static_cast<unsigned>(n);
        }
        stream.next_out = out.data();
        stream.avail_out = static_cast<unsigned>(out.size());
        const unsigned before = stream.avail_in;
        result = mz_inflate(&stream, MZ_NO_FLUSH);
        size_t n = out.size() - stream.avail_out;
        if (n > entry.unpacked_size - written ||
            (result != MZ_OK && result != MZ_STREAM_END) ||
            (n == 0 && before == stream.avail_in && result != MZ_STREAM_END)) {
            ok = false; break;
        }
        if (std::fwrite(out.data(), 1, n, output) != n) { ok = false; break; }
        output_crc = pvz2_cache_crc32(output_crc, out.data(), n);
        written += static_cast<uint32_t>(n);
        g_written.store(written, std::memory_order_relaxed);
    }
    /* Include the archive's alignment padding in the source fingerprint. */
    while (ok && remaining) {
        size_t n = remaining < in.size() ? remaining : in.size();
        if (std::fread(in.data(), 1, n, input) != n) { ok = false; break; }
        source_crc = pvz2_cache_crc32(source_crc, in.data(), n);
        remaining -= n;
    }
    if (initialized) mz_inflateEnd(&stream);
    std::fclose(input);
    if (std::fclose(output) != 0) ok = false;
    ok = ok && written == entry.unpacked_size && result == MZ_STREAM_END;
    if (ok) {
        std::remove(path);
        ok = std::rename(partial.c_str(), path) == 0;
    }
    if (!ok) {
        g_active_block.store(0, std::memory_order_relaxed);
        ++g_cache_failures;
        std::remove(partial.c_str());
        l_error("[rsb-cache] failed block=0x%x output=%u expected=%u z=%d",
                entry.block_offset, written, entry.unpacked_size, result);
        return false;
    }
    entry.cache_path = path;
    g_block_cache.emplace(entry.block_offset, entry.cache_path);
    g_active_block.store(0, std::memory_order_relaxed);
    ++g_blocks_ready;
    /* Publish validation metadata only after the complete data file. Failure
     * to save metadata merely causes normal extraction next time. */
    const std::string meta = std::string(path) + ".meta";
    const std::string temp_meta = meta + ".tmp";
    FILE *mf = std::fopen(temp_meta.c_str(), "wb");
    if (mf) {
        const uint32_t h[] = {0x32434250u, 1, source_crc, output_crc, entry.block_size, entry.unpacked_size};
        bool saved = std::fwrite(h, sizeof(h), 1, mf) == 1;
        if (std::fclose(mf) != 0) saved = false;
        if (saved) { std::remove(meta.c_str()); std::rename(temp_meta.c_str(), meta.c_str()); }
        else std::remove(temp_meta.c_str());
    }
    l_info("[rsb-cache] block=0x%x extracted=%u", entry.block_offset, written);
    return true;
}

/* Index keys and all fields except cache_path are immutable after load_index.
 * Extraction holds its own lock: unrelated lookups must not wait for disk I/O
 * or decompression. Return a stable entry after just one normalize/find. */
static Entry *rsb_lookup_entry(const char *name) {
    ++g_queries;
    const std::string key = normalize(name);
    pthread_mutex_lock(&g_lock);
    if (!g_tried) { g_tried = true; g_loaded = load_index(); }
    Entry *entry = nullptr;
    if (g_loaded) {
        auto it = g_entries.find(key);
        if (it != g_entries.end()) entry = &it->second;
    }
    pthread_mutex_unlock(&g_lock);
    if (!entry && name && *name) ++g_misses;
    return entry;
}

extern "C" const char *vita_rsb_locate(const char *name, uint64_t *offset, uint32_t *size) {
    Entry *found = rsb_lookup_entry(name);
    if (!found || found->compressed) return nullptr;
    auto &entry = *found;
    const char *path = pvz2_obb_path();
    if (entry.block_size) {
        pthread_mutex_lock(&g_cache_lock);
        if (!cache_block(entry)) path = nullptr;
        else path = entry.cache_path.c_str();
        pthread_mutex_unlock(&g_cache_lock);
    }
    if (path) {
        if (offset) *offset = entry.block_size ? entry.within_block : entry.offset;
        if (size) *size = entry.size;
    }
    return path;
}

extern "C" int vita_rsb_find(const char *guest_path, uint64_t *offset,
                              uint32_t *size, int *compressed) {
    const Entry *entry = rsb_lookup_entry(guest_path);
    if (!entry) return 0;
    if (offset) *offset = entry->offset;
    if (size) *size = entry->size;
    if (compressed) *compressed = entry->compressed ? 1 : 0;
    return 1;
}

/* No index lock: telemetry must keep running while a worker extracts a block. */
extern "C" void vita_rsb_format_stats(char *out, size_t capacity) {
    std::snprintf(out, capacity, "queries=%u misses=%u extracted=%u errors=%u active=0x%x bytes=%u/%u",
        g_queries.load(), g_misses.load(), g_blocks_ready.load(), g_cache_failures.load(),
        g_active_block.load(), g_written.load(), g_expected.load());
}

extern "C" const char *vita_rsb_obb_path(void) {
    return pvz2_obb_path();
}

static void *preload_index_worker(void *) {
    const uint64_t begin = sceKernelGetSystemTimeWide();
    /* Use the normal serialized path: early consumers wait for publication,
     * and there is never a second parser mutating the index concurrently. */
    vita_rsb_find("", nullptr, nullptr, nullptr);
    telemetry_log("ASSETS", "background index finished in %u ms",
                  (unsigned)((sceKernelGetSystemTimeWide() - begin) / 1000));
    return nullptr;
}

extern "C" void vita_rsb_start_preload(void) {
    pthread_t worker;
    int rc = pthread_create_soloader(&worker, nullptr, preload_index_worker, nullptr);
    if (rc == 0) pthread_detach(worker);
    else telemetry_log("ASSETS", "background index unavailable (%d); using normal on-demand load", rc);
}
