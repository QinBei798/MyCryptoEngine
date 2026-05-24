#include "sm4z_archiver.h"

#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

std::vector<uint8_t> generate_random_data(size_t size) {
    std::vector<uint8_t> data(size);
    std::random_device rd;
    std::mt19937_64 rng(rd());
    std::uniform_int_distribution<int> dist(0, 255);
    for (size_t i = 0; i < size; ++i)
        data[i] = static_cast<uint8_t>(dist(rng));
    return data;
}

void write_file(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
}

std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    auto size = in.tellg();
    in.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    in.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

} // namespace

// ──────────────────────────────────────────────
// Test 1: Multi-chunk round-trip integrity
// ──────────────────────────────────────────────
TEST(SM4ZArchiverTest, MultiChunkPackAndUnpackRoundTrip) {
    constexpr size_t kDataSize = 10 * 1024 * 1024; // 10 MB → spans multiple chunks
    auto original = generate_random_data(kDataSize);

    const std::string src_path  = "/tmp/test_sm4z_roundtrip_input.bin";
    const std::string sm4z_path = "/tmp/test_sm4z_roundtrip.sm4z";
    const std::string dest_path = "/tmp/test_sm4z_roundtrip_output.bin";
    const std::string password  = "test_password_multi_chunk";

    write_file(src_path, original);

    SM4ZArchiver archiver;

    bool pack_ok = archiver.pack_archive(src_path, sm4z_path, password);
    ASSERT_TRUE(pack_ok) << "pack_archive failed";

    bool unpack_ok = archiver.unpack_archive(sm4z_path, dest_path, password);
    ASSERT_TRUE(unpack_ok) << "unpack_archive failed";

    auto recovered = read_file(dest_path);
    ASSERT_EQ(recovered.size(), original.size());
    ASSERT_EQ(std::memcmp(original.data(), recovered.data(), original.size()), 0)
        << "Round-trip data mismatch";

    std::remove(src_path.c_str());
    std::remove(sm4z_path.c_str());
    std::remove(dest_path.c_str());
}

// ──────────────────────────────────────────────
// Test 2: Header tamper detection
// ──────────────────────────────────────────────
TEST(SM4ZArchiverTest, HeaderTamperRefusal) {
    constexpr size_t kDataSize = 1024 * 1024; // 1 MB
    auto original = generate_random_data(kDataSize);

    const std::string src_path  = "/tmp/test_sm4z_tamper_input.bin";
    const std::string sm4z_path = "/tmp/test_sm4z_tamper.sm4z";
    const std::string dest_path = "/tmp/test_sm4z_tamper_output.bin";
    const std::string password  = "test_password_tamper";

    write_file(src_path, original);

    SM4ZArchiver archiver;
    ASSERT_TRUE(archiver.pack_archive(src_path, sm4z_path, password));

    // Flip bit 3 at offset 10 (inside header, before Header MAC)
    {
        std::fstream f(sm4z_path, std::ios::in | std::ios::out | std::ios::binary);
        ASSERT_TRUE(f.is_open());
        f.seekp(10);
        char byte_val;
        f.get(byte_val);
        f.seekp(10);
        f.put(static_cast<char>(byte_val ^ 0x08));
        f.close();
    }

    bool unpack_ok = archiver.unpack_archive(sm4z_path, dest_path, password);
    EXPECT_FALSE(unpack_ok) << "unpack_archive should reject tampered header";

    std::remove(src_path.c_str());
    std::remove(sm4z_path.c_str());
    std::remove(dest_path.c_str());
}

// ──────────────────────────────────────────────
// Test 3: Truncated file (missing trailing MAC)
// ──────────────────────────────────────────────
TEST(SM4ZArchiverTest, TruncatedFileMitigation) {
    constexpr size_t kDataSize = 1024 * 1024; // 1 MB
    auto original = generate_random_data(kDataSize);

    const std::string src_path  = "/tmp/test_sm4z_trunc_input.bin";
    const std::string sm4z_path = "/tmp/test_sm4z_trunc.sm4z";
    const std::string dest_path = "/tmp/test_sm4z_trunc_output.bin";
    const std::string password  = "test_password_trunc";

    write_file(src_path, original);

    SM4ZArchiver archiver;
    ASSERT_TRUE(archiver.pack_archive(src_path, sm4z_path, password));

    // Truncate last 32 bytes (final chunk MAC)
    {
        auto file_data = read_file(sm4z_path);
        ASSERT_GT(file_data.size(), 32u);
        file_data.resize(file_data.size() - 32);
        write_file(sm4z_path, file_data);
    }

    // Must fail cleanly — no segfault, no uncaught exception
    bool unpack_ok = archiver.unpack_archive(sm4z_path, dest_path, password);
    EXPECT_FALSE(unpack_ok) << "unpack_archive should reject truncated file";

    std::remove(src_path.c_str());
    std::remove(sm4z_path.c_str());
    std::remove(dest_path.c_str());
}

// ──────────────────────────────────────────────
// Test 4: Parallel large-file round-trip (40 MB, 4 threads)
// ──────────────────────────────────────────────
TEST(SM4ZArchiverTest, ParallelLargeFileRoundTrip) {
    constexpr size_t kDataSize = 40 * 1024 * 1024; // 40 MB → ~640 chunks of 64KB
    auto original = generate_random_data(kDataSize);

    const std::string src_path  = "/tmp/test_sm4z_parallel_input.bin";
    const std::string sm4z_path = "/tmp/test_sm4z_parallel.sm4z";
    const std::string dest_path = "/tmp/test_sm4z_parallel_output.bin";
    const std::string password  = "test_password_parallel_40mb";

    write_file(src_path, original);

    SM4ZArchiver archiver;
    archiver.set_num_threads(4);

    bool pack_ok = archiver.pack_archive(src_path, sm4z_path, password);
    ASSERT_TRUE(pack_ok) << "parallel pack_archive failed";

    bool unpack_ok = archiver.unpack_archive(sm4z_path, dest_path, password);
    ASSERT_TRUE(unpack_ok) << "unpack_archive failed";

    auto recovered = read_file(dest_path);
    ASSERT_EQ(recovered.size(), original.size());
    ASSERT_EQ(std::memcmp(original.data(), recovered.data(), original.size()), 0)
        << "Parallel round-trip data mismatch";

    std::remove(src_path.c_str());
    std::remove(sm4z_path.c_str());
    std::remove(dest_path.c_str());
}

// ──────────────────────────────────────────────
// Test 5: Parallel determinism — unpacked content is identical regardless
//         of thread count (random salt means .sm4z files differ on disk,
//         but the recovered plaintext MUST match the original).
// ──────────────────────────────────────────────
TEST(SM4ZArchiverTest, ParallelDeterministicEquivalence) {
    constexpr size_t kDataSize = 4 * 1024 * 1024; // 4 MB
    auto original = generate_random_data(kDataSize);

    const std::string src_path     = "/tmp/test_sm4z_det_input.bin";
    const std::string sm4z_st_path  = "/tmp/test_sm4z_det_single.sm4z";
    const std::string sm4z_mt_path  = "/tmp/test_sm4z_det_multi.sm4z";
    const std::string dest_st_path  = "/tmp/test_sm4z_det_single_out.bin";
    const std::string dest_mt_path  = "/tmp/test_sm4z_det_multi_out.bin";
    const std::string password     = "test_password_determinism";

    write_file(src_path, original);

    SM4ZArchiver archiver_st;
    archiver_st.set_num_threads(0);
    ASSERT_TRUE(archiver_st.pack_archive(src_path, sm4z_st_path, password));
    ASSERT_TRUE(archiver_st.unpack_archive(sm4z_st_path, dest_st_path, password));

    SM4ZArchiver archiver_mt;
    archiver_mt.set_num_threads(4);
    ASSERT_TRUE(archiver_mt.pack_archive(src_path, sm4z_mt_path, password));
    ASSERT_TRUE(archiver_mt.unpack_archive(sm4z_mt_path, dest_mt_path, password));

    // Both unpacked outputs must match the original byte-for-byte
    auto st_out = read_file(dest_st_path);
    auto mt_out = read_file(dest_mt_path);

    ASSERT_EQ(st_out.size(), original.size());
    ASSERT_EQ(mt_out.size(), original.size());
    ASSERT_EQ(std::memcmp(st_out.data(), original.data(), original.size()), 0)
        << "Single-threaded unpack mismatch";
    ASSERT_EQ(std::memcmp(mt_out.data(), original.data(), original.size()), 0)
        << "Multi-threaded unpack mismatch";

    std::remove(src_path.c_str());
    std::remove(sm4z_st_path.c_str());
    std::remove(sm4z_mt_path.c_str());
    std::remove(dest_st_path.c_str());
    std::remove(dest_mt_path.c_str());
}
