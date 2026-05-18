#pragma once

#include <cstdint>
#include <vector>

#include <immintrin.h>

class SM4AVX2 {
public:
    explicit SM4AVX2(const std::vector<uint8_t>& key);
    ~SM4AVX2();

    SM4AVX2(const SM4AVX2&) = delete;
    SM4AVX2& operator=(const SM4AVX2&) = delete;
    SM4AVX2(SM4AVX2&&) = default;
    SM4AVX2& operator=(SM4AVX2&&) = default;

    std::vector<uint8_t> encrypt_ctr(const std::vector<uint8_t>& iv,
                                     const std::vector<uint8_t>& plaintext) const;

    // Phase B: generate 16 consecutive big-endian counter blocks
    // into word-sliced layout (8 × __m256i = 2 batches × 4 words).
    static void generate_16_counters(__m256i* state_matrix,
                                     const uint8_t* initial_counter);

    // Phase C: AVX2 parallel S-box (τ transform) on 16 32-bit words.
    // in_out_words[0..1] each hold 8 words; overwritten in-place.
    static void parallel_sbox_16(__m256i* in_out_words);

    // Phase D: AVX2 parallel L transform on 8 32-bit words.
    // L(B) = B ⊕ (B<<<2) ⊕ (B<<<10) ⊕ (B<<<18) ⊕ (B<<<24)
    static __m256i parallel_L_transform(__m256i b);

private:
    void key_expansion(const uint8_t key[16]);

    alignas(32) uint32_t rk_[32];
};
