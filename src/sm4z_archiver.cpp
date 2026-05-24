#include "sm4z_archiver.h"

#include <openssl/crypto.h>
#include <openssl/rand.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <future>
#include <stdexcept>
#include <vector>

#include "crypto_utils.h"
#include "secure_chunk_pipeline.h"

// ═══════════════════════════════════════════════════════════════════════════
//  .sm4z V3 Container Constants
// ═══════════════════════════════════════════════════════════════════════════

static constexpr uint8_t  kMagic[4]       = {'S', 'M', '4', 'Z'};
static constexpr uint32_t kVersion         = 0x00000003;
static constexpr size_t   kHeaderPrefixLen = 48;   // magic(4) + version(4) + salt(16) + iv(16) + chunks(8)
static constexpr size_t   kHmacLen         = 32;
static constexpr size_t   kHeaderLen       = kHeaderPrefixLen + kHmacLen; // 80
static constexpr size_t   kChunkIndexEntry = 16;   // file_offset(8) + compressed_size(4) + original_size(4)
static constexpr size_t   kChunkSize       = 64 * 1024;      // 64 KB
static constexpr size_t   kMacroChunkSize  = 4 * 1024 * 1024; // 4 MB — parallel task unit

// ═══════════════════════════════════════════════════════════════════════════
//  LE serialization helpers
// ═══════════════════════════════════════════════════════════════════════════

namespace {

void write_u32_le(std::vector<uint8_t>& buf, uint32_t val)
{
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
}

void write_u64_le(std::vector<uint8_t>& buf, uint64_t val)
{
    for (int i = 0; i < 8; ++i)
        buf.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
}

uint32_t read_u32_le(const uint8_t* src)
{
    return static_cast<uint32_t>(src[0])
         | (static_cast<uint32_t>(src[1]) << 8)
         | (static_cast<uint32_t>(src[2]) << 16)
         | (static_cast<uint32_t>(src[3]) << 24);
}

uint64_t read_u64_le(const uint8_t* src)
{
    uint64_t val = 0;
    for (int i = 0; i < 8; ++i)
        val |= static_cast<uint64_t>(src[i]) << (i * 8);
    return val;
}

std::vector<uint8_t> read_file(const std::string& path)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
        return {};
    auto size = in.tellg();
    in.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    in.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

bool write_file(const std::string& path, const std::vector<uint8_t>& data)
{
    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    out.close();
    return out.good();
}

// Derive per-chunk IV from base IV + chunk index.
// XORs the chunk index (LE) into the last 8 bytes of base_iv.
std::vector<uint8_t> derive_chunk_iv(const std::vector<uint8_t>& base_iv,
                                     uint64_t chunk_idx)
{
    std::vector<uint8_t> iv = base_iv;
    for (int i = 0; i < 8; ++i)
        iv[8 + i] ^= static_cast<uint8_t>((chunk_idx >> (i * 8)) & 0xFF);
    return iv;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
//  SM4ZArchiver
// ═══════════════════════════════════════════════════════════════════════════

SM4ZArchiver::SM4ZArchiver() {}
SM4ZArchiver::~SM4ZArchiver() {}

void SM4ZArchiver::set_num_threads(int n)
{
    num_threads_ = n;
}

bool SM4ZArchiver::pack_archive(const std::string& src_file_path,
                                 const std::string& out_sm4z_path,
                                 const std::string& password)
{
    // 1. Read source file
    std::vector<uint8_t> plaintext = read_file(src_file_path);

    // 2. Generate random salt and base IV
    std::vector<uint8_t> salt(16);
    std::vector<uint8_t> base_iv(16);
    if (RAND_bytes(salt.data(), 16) != 1 || RAND_bytes(base_iv.data(), 16) != 1)
        return false;

    // 3. Derive keys from password + salt
    std::vector<uint8_t> sm4_key, hmac_key;
    derive_keys(password, salt, sm4_key, hmac_key);

    struct ChunkDesc {
        uint64_t file_offset;
        uint32_t compressed_size;
        uint32_t original_size;
        std::vector<uint8_t> sealed;
    };
    std::vector<ChunkDesc> chunks;

    if (num_threads_ <= 1) {
        // ── Sequential path (unchanged) ──────────────────────────
        SecureChunkPipeline pipeline;

        size_t offset = 0;
        uint64_t chunk_idx = 0;
        while (offset < plaintext.size()) {
            size_t len = std::min(kChunkSize, plaintext.size() - offset);
            std::vector<uint8_t> chunk_pt(
                plaintext.begin() + static_cast<ptrdiff_t>(offset),
                plaintext.begin() + static_cast<ptrdiff_t>(offset + len));

            uint64_t orig_sz = 0;
            std::vector<uint8_t> chunk_iv = derive_chunk_iv(base_iv, chunk_idx);
            std::vector<uint8_t> sealed = pipeline.seal_chunk(
                chunk_pt, chunk_iv, sm4_key, hmac_key, orig_sz);

            ChunkDesc desc;
            desc.file_offset     = 0;
            desc.compressed_size = static_cast<uint32_t>(sealed.size());
            desc.original_size   = static_cast<uint32_t>(orig_sz);
            desc.sealed          = std::move(sealed);
            chunks.push_back(std::move(desc));

            offset += len;
            ++chunk_idx;
        }
    } else {
        // ── Parallel path ────────────────────────────────────────
        int n_workers = num_threads_;

        // Partition plaintext into 4 MB macro-chunks
        struct MacroTask {
            uint64_t start_chunk_idx;
            size_t   data_offset;
            size_t   data_len;
        };
        std::vector<MacroTask> tasks;

        {
            size_t off = 0;
            uint64_t ci = 0;
            while (off < plaintext.size()) {
                size_t macro_len = std::min(kMacroChunkSize, plaintext.size() - off);
                uint64_t micro_count = (macro_len + kChunkSize - 1) / kChunkSize;

                MacroTask mt;
                mt.start_chunk_idx = ci;
                mt.data_offset     = off;
                mt.data_len        = macro_len;
                tasks.push_back(mt);

                off += macro_len;
                ci  += micro_count;
            }
        }

        // Cap workers to number of tasks
        if (static_cast<size_t>(n_workers) > tasks.size())
            n_workers = static_cast<int>(tasks.size());

        // Each worker returns a vector of sealed micro-chunks for its macro-task.
        using MicroResult = std::pair<std::vector<uint8_t>, uint32_t>; // sealed, original_size

        auto worker = [&plaintext, &base_iv, &sm4_key, &hmac_key](
                          const MacroTask& task) -> std::vector<MicroResult>
        {
            // Allocate 32-byte-aligned working buffer for this macro-chunk
            uint8_t* buf = static_cast<uint8_t*>(
                std::aligned_alloc(32, task.data_len));
            if (!buf)
                throw std::bad_alloc();

            std::memcpy(buf, plaintext.data() + task.data_offset, task.data_len);

            SecureChunkPipeline pipeline; // thread-local instance
            std::vector<MicroResult> results;

            size_t   off = 0;
            uint64_t ci  = task.start_chunk_idx;
            while (off < task.data_len) {
                size_t micro_len = std::min(kChunkSize, task.data_len - off);
                std::vector<uint8_t> micro_pt(buf + off, buf + off + micro_len);

                uint64_t orig_sz = 0;
                std::vector<uint8_t> chunk_iv = derive_chunk_iv(base_iv, ci);
                std::vector<uint8_t> sealed = pipeline.seal_chunk(
                    micro_pt, chunk_iv, sm4_key, hmac_key, orig_sz);

                results.emplace_back(std::move(sealed),
                                     static_cast<uint32_t>(orig_sz));

                off += micro_len;
                ++ci;
            }

            OPENSSL_cleanse(buf, task.data_len);
            std::free(buf);
            return results;
        };

        // Dispatch workers
        std::vector<std::future<std::vector<MicroResult>>> futures;
        futures.reserve(tasks.size());
        for (const auto& t : tasks)
            futures.push_back(std::async(std::launch::async, worker, t));

        // Collect results in strict macro-chunk order → deterministic
        for (auto& fut : futures) {
            std::vector<MicroResult> worker_results = fut.get();
            for (auto& [sealed, orig_sz] : worker_results) {
                ChunkDesc desc;
                desc.file_offset     = 0;
                desc.compressed_size = static_cast<uint32_t>(sealed.size());
                desc.original_size   = orig_sz;
                desc.sealed          = std::move(sealed);
                chunks.push_back(std::move(desc));
            }
        }
    }

    if (chunks.empty()) {
        // Edge case: empty input → write a valid archive with 0 chunks
        std::vector<uint8_t> hdr_prefix;
        hdr_prefix.reserve(kHeaderPrefixLen);
        hdr_prefix.insert(hdr_prefix.end(), kMagic, kMagic + 4);
        write_u32_le(hdr_prefix, kVersion);
        hdr_prefix.insert(hdr_prefix.end(), salt.begin(), salt.end());
        hdr_prefix.insert(hdr_prefix.end(), base_iv.begin(), base_iv.end());
        write_u64_le(hdr_prefix, 0);

        std::vector<uint8_t> header_mac = calc_hmac_sm3(hmac_key, hdr_prefix);

        std::vector<uint8_t> out_buf;
        out_buf.reserve(kHeaderLen);
        out_buf.insert(out_buf.end(), hdr_prefix.begin(), hdr_prefix.end());
        out_buf.insert(out_buf.end(), header_mac.begin(), header_mac.end());
        return write_file(out_sm4z_path, out_buf);
    }

    // 4. Patch file offsets
    uint64_t data_start = kHeaderLen + chunks.size() * kChunkIndexEntry;
    uint64_t cursor = data_start;
    for (auto& c : chunks) {
        c.file_offset = cursor;
        cursor += c.sealed.size();
    }

    // 5. Build header prefix (first 48 bytes)
    std::vector<uint8_t> hdr_prefix;
    hdr_prefix.reserve(kHeaderPrefixLen);
    hdr_prefix.insert(hdr_prefix.end(), kMagic, kMagic + 4);
    write_u32_le(hdr_prefix, kVersion);
    hdr_prefix.insert(hdr_prefix.end(), salt.begin(), salt.end());
    hdr_prefix.insert(hdr_prefix.end(), base_iv.begin(), base_iv.end());
    write_u64_le(hdr_prefix, static_cast<uint64_t>(chunks.size()));

    std::vector<uint8_t> header_mac = calc_hmac_sm3(hmac_key, hdr_prefix);

    // 6. Serialize to buffer, then write in one shot
    std::vector<uint8_t> out_buf;
    out_buf.reserve(data_start + (cursor - data_start));

    out_buf.insert(out_buf.end(), hdr_prefix.begin(), hdr_prefix.end());
    out_buf.insert(out_buf.end(), header_mac.begin(), header_mac.end());

    for (const auto& c : chunks) {
        write_u64_le(out_buf, c.file_offset);
        write_u32_le(out_buf, c.compressed_size);
        write_u32_le(out_buf, c.original_size);
    }

    for (const auto& c : chunks)
        out_buf.insert(out_buf.end(), c.sealed.begin(), c.sealed.end());

    return write_file(out_sm4z_path, out_buf);
}

bool SM4ZArchiver::unpack_archive(const std::string& src_sm4z_path,
                                   const std::string& out_dest_path,
                                   const std::string& password)
{
    // 1. Read entire archive
    std::vector<uint8_t> file_data = read_file(src_sm4z_path);
    if (file_data.size() < kHeaderLen)
        return false;

    const uint8_t* data = file_data.data();
    size_t file_size     = file_data.size();

    // 2. Verify magic + version
    if (std::memcmp(data, kMagic, 4) != 0)
        return false;
    if (read_u32_le(data + 4) != kVersion)
        return false;

    // 3. Extract salt, base IV, total chunks
    std::vector<uint8_t> salt(data + 8, data + 24);
    std::vector<uint8_t> base_iv(data + 24, data + 40);
    uint64_t total_chunks = read_u64_le(data + 40);

    // 4. Derive keys
    std::vector<uint8_t> sm4_key, hmac_key;
    derive_keys(password, salt, sm4_key, hmac_key);

    // 5. Verify Header MAC (first 48 bytes) BEFORE parsing index
    {
        std::vector<uint8_t> hdr_prefix(data, data + kHeaderPrefixLen);
        std::vector<uint8_t> expected_mac = calc_hmac_sm3(hmac_key, hdr_prefix);
        if (CRYPTO_memcmp(expected_mac.data(), data + kHeaderPrefixLen, kHmacLen) != 0)
            return false; // wrong password or tampered header
    }

    // 6. Bounds-check the chunk index table
    size_t index_offset = kHeaderLen;
    size_t index_size   = static_cast<size_t>(total_chunks) * kChunkIndexEntry;
    if (index_offset + index_size > file_size)
        return false;

    // 7. Parse chunk index
    struct IdxEntry {
        uint64_t file_offset;
        uint32_t compressed_size;
        uint32_t original_size;
    };
    std::vector<IdxEntry> index;
    index.reserve(static_cast<size_t>(total_chunks));
    for (uint64_t i = 0; i < total_chunks; ++i) {
        const uint8_t* p = data + index_offset + i * kChunkIndexEntry;
        IdxEntry e;
        e.file_offset     = read_u64_le(p);
        e.compressed_size = read_u32_le(p + 8);
        e.original_size   = read_u32_le(p + 12);
        index.push_back(e);
    }

    // 8. Process each chunk: verify MAC, decrypt, decompress
    SecureChunkPipeline pipeline;
    std::vector<uint8_t> recovered;
    recovered.reserve(static_cast<size_t>(
        total_chunks > 0 ? total_chunks * kChunkSize / 2 : 0));

    for (uint64_t i = 0; i < total_chunks; ++i) {
        const auto& e = index[i];

        // Bounds-check chunk data
        if (e.file_offset > file_size
            || e.compressed_size > file_size - e.file_offset)
            return false;

        std::vector<uint8_t> chunk_iv = derive_chunk_iv(base_iv, i);

        std::vector<uint8_t> sealed(data + e.file_offset,
                                     data + e.file_offset + e.compressed_size);

        try {
            std::vector<uint8_t> plain = pipeline.open_chunk(
                sealed, chunk_iv, sm4_key, hmac_key, e.original_size);
            recovered.insert(recovered.end(), plain.begin(), plain.end());
        } catch (const std::runtime_error&) {
            return false; // HMAC mismatch, decompression failure, etc.
        }
    }

    // 9. Write recovered plaintext
    return write_file(out_dest_path, recovered);
}
