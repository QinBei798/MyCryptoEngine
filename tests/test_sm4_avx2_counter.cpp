#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

#include "sm4_avx2.h"

// ---------------------------------------------------------------------------
// Phase B Step A: AVX2 parallel counter generation — RED phase
// The stub is zero-filled; this test MUST fail (assertion mismatch).
// ---------------------------------------------------------------------------

namespace {

// Verified scalar big-endian 128-bit counter increment.
// Byte-identical to the logic inside SM4Standard::encrypt_ctr.
inline void scalar_ctr_increment(uint8_t counter[16]) {
    for (int i = 15; i >= 0; --i) {
        if (++counter[i] != 0) break;
    }
}

// Pack 16 consecutive counter blocks (block-sliced) into the same
// word-sliced layout that generate_16_counters produces:
//   8 × __m256i  =  2 batches × 4 words each
//   state_matrix[batch*4 + word] holds word 'word' of counters batch*8 .. batch*8+7
void pack_expected_word_sliced(const uint8_t blocks[256],
                               uint8_t out_ws[256]) {
    for (int batch = 0; batch < 2; ++batch) {
        for (int word = 0; word < 4; ++word) {
            alignas(32) uint32_t words[8];
            for (int b = 0; b < 8; ++b) {
                const uint8_t* blk = blocks + (batch * 8 + b) * 16 + word * 4;
                // Load as LE (same as _mm256_i32gather_epi32 from BE bytes)
                words[b] = static_cast<uint32_t>(blk[0]) |
                           (static_cast<uint32_t>(blk[1]) << 8) |
                           (static_cast<uint32_t>(blk[2]) << 16) |
                           (static_cast<uint32_t>(blk[3]) << 24);
            }
            auto vec = _mm256_load_si256(
                reinterpret_cast<const __m256i*>(words));
            _mm256_store_si256(
                reinterpret_cast<__m256i*>(out_ws + batch * 128 + word * 32),
                vec);
        }
    }
}

} // namespace

TEST(SM4AVX2CounterTest, Generate16CountersMatchesScalarGroundTruth) {
    // ── 1. Initial counter with carry-crossing boundary ──
    // Bytes 14-15 = 0xFF, 0xFF → next increment carries into byte 13
    alignas(32) const uint8_t initial_counter[16] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    };

    // ── 2. Generate ground truth: 16 consecutive counters (scalar) ──
    alignas(32) uint8_t expected_blocks[256];
    {
        uint8_t ctr[16];
        std::memcpy(ctr, initial_counter, 16);
        for (int b = 0; b < 16; ++b) {
            std::memcpy(expected_blocks + b * 16, ctr, 16);
            scalar_ctr_increment(ctr);
        }
    }

    // Convert to word-sliced layout for comparison
    alignas(32) uint8_t expected_ws[256];
    pack_expected_word_sliced(expected_blocks, expected_ws);

    // ── 3. Call the AVX2 stub ──
    alignas(32) uint8_t state_matrix_raw[256];
    auto* state_matrix = reinterpret_cast<__m256i*>(state_matrix_raw);
    SM4AVX2::generate_16_counters(state_matrix, initial_counter);

    // ── 4. Assert byte-identical match ──
    // The stub fills with _mm256_setzero_si256, so this WILL fail.
    EXPECT_EQ(std::memcmp(state_matrix_raw, expected_ws, 256), 0)
        << "generate_16_counters output does not match scalar ground truth";
}
