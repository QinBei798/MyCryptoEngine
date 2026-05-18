#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <malloc.h>
#include <random>
#include <vector>

#include "sm4_avx2.h"
#include "sm4_standard.h"

// ---------------------------------------------------------------------------
// Phase A: AVX2 memory-bus alignment verification.
// Goal — prove _mm256_load_si256 / _mm256_store_si256 don't segfault
// when buffers are properly aligned.  No encryption comparison yet.
// ---------------------------------------------------------------------------

static std::vector<uint8_t> random_key() {
    std::vector<uint8_t> key(16);
    std::mt19937_64 rng(0xCAFE);
    for (auto& b : key) b = static_cast<uint8_t>(rng() & 0xFF);
    return key;
}

// Allocate 32-byte-aligned heap buffer using _mm_malloc.
// Returns a raw pointer that must be freed with _mm_free.
struct AlignedBuffer {
    uint8_t* ptr;
    size_t   size;

    AlignedBuffer(size_t s) : size(s) {
        ptr = static_cast<uint8_t*>(_mm_malloc(size, 32));
        // Use EXPECT (not ASSERT) — constructors can't return void
        EXPECT_NE(ptr, nullptr) << "_mm_malloc returned null";
        EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % 32, 0u)
            << "buffer not 32-byte aligned";
    }

    ~AlignedBuffer() { if (ptr) _mm_free(ptr); }
    AlignedBuffer(const AlignedBuffer&) = delete;
    AlignedBuffer& operator=(const AlignedBuffer&) = delete;
};

TEST(SM4AVX2AlignTest, LoadStore256BytesNoSegfault) {
    constexpr size_t kSize = 1 * 1024 * 1024;  // 1 MB
    auto key = random_key();
    SM4AVX2 cipher(key);

    AlignedBuffer plaintext(kSize);
    AlignedBuffer iv(16);

    // Fill plaintext with deterministic pseudo-random data
    std::mt19937_64 rng(0xBEEF);
    for (size_t i = 0; i < kSize; ++i) {
        plaintext.ptr[i] = static_cast<uint8_t>(rng() & 0xFF);
    }
    std::memset(iv.ptr, 0, 16);

    // Wrap aligned buffers in vectors for the existing interface.
    // NOTE: std::vector copies — the internal _mm_malloc'd memory inside
    // encrypt_ctr is what actually touches the 256-bit bus.
    std::vector<uint8_t> pt_vec(plaintext.ptr, plaintext.ptr + kSize);
    std::vector<uint8_t> iv_vec(iv.ptr, iv.ptr + 16);

    // If this returns without segfault, the memory bus is proven stable.
    auto ct_vec = cipher.encrypt_ctr(iv_vec, pt_vec);

    ASSERT_EQ(ct_vec.size(), kSize);
    // Phase A is about proving no crash — content correctness comes later.
    SUCCEED() << "AVX2 aligned load/store completed without segfault on "
              << kSize << " bytes";
}

TEST(SM4AVX2AlignTest, MisalignedSourceDoesNotCrash) {
    // The public API accepts std::vector<uint8_t> which may be unaligned.
    // Our encrypt_ctr copies into an aligned internal buffer first,
    // so this must not segfault even for oddly-sized vectors.
    // Deliberately odd sizes to stress alignment
    for (size_t size : {1ul, 7ul, 13ul, 16ul, 127ul, 255ul, 256ul, 333ul, 1024ul}) {
        auto key = random_key();
        SM4AVX2 cipher(key);
        std::vector<uint8_t> pt(size);
        std::vector<uint8_t> iv(16, 0);
        for (size_t i = 0; i < size; ++i) pt[i] = static_cast<uint8_t>(i & 0xFF);

        auto ct = cipher.encrypt_ctr(iv, pt);
        ASSERT_EQ(ct.size(), size);
    }
    SUCCEED() << "All odd-size inputs passed without segfault";
}

// ---------------------------------------------------------------------------
// Phase B: Real encryption — SM4AVX2 must match SM4Standard byte-for-byte
// ---------------------------------------------------------------------------

TEST(SM4AVX2ConsistencyTest, EncryptCTRMatchesScalarBaseline1MB) {
    constexpr size_t kSize = 1 * 1024 * 1024;  // 1 MB
    auto key = random_key();

    SM4Standard scalar(key);
    SM4AVX2 avx2(key);

    std::mt19937_64 rng(0xDEAD);
    std::vector<uint8_t> plaintext(kSize);
    std::vector<uint8_t> iv(16, 0);
    for (size_t i = 0; i < kSize; ++i)
        plaintext[i] = static_cast<uint8_t>(rng() & 0xFF);
    for (size_t i = 0; i < 16; ++i)
        iv[i] = static_cast<uint8_t>((rng() >> 8) & 0xFF);

    auto ct_scalar = scalar.encrypt_ctr(iv, plaintext);
    auto ct_avx2   = avx2.encrypt_ctr(iv, plaintext);

    ASSERT_EQ(ct_avx2.size(), ct_scalar.size());
    EXPECT_EQ(ct_avx2, ct_scalar)
        << "AVX2 ciphertext differs from scalar baseline at 1MB";
}
