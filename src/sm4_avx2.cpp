#include "sm4_avx2.h"

#include <cstring>
#include <immintrin.h>
#include <malloc.h>

namespace {

alignas(32) constexpr uint8_t SBOX[256] = {
    0xd6, 0x90, 0xe9, 0xfe, 0xcc, 0xe1, 0x3d, 0xb7,
    0x16, 0xb6, 0x14, 0xc2, 0x28, 0xfb, 0x2c, 0x05,
    0x2b, 0x67, 0x9a, 0x76, 0x2a, 0xbe, 0x04, 0xc3,
    0xaa, 0x44, 0x13, 0x26, 0x49, 0x86, 0x06, 0x99,
    0x9c, 0x42, 0x50, 0xf4, 0x91, 0xef, 0x98, 0x7a,
    0x33, 0x54, 0x0b, 0x43, 0xed, 0xcf, 0xac, 0x62,
    0xe4, 0xb3, 0x1c, 0xa9, 0xc9, 0x08, 0xe8, 0x95,
    0x80, 0xdf, 0x94, 0xfa, 0x75, 0x8f, 0x3f, 0xa6,
    0x47, 0x07, 0xa7, 0xfc, 0xf3, 0x73, 0x17, 0xba,
    0x83, 0x59, 0x3c, 0x19, 0xe6, 0x85, 0x4f, 0xa8,
    0x68, 0x6b, 0x81, 0xb2, 0x71, 0x64, 0xda, 0x8b,
    0xf8, 0xeb, 0x0f, 0x4b, 0x70, 0x56, 0x9d, 0x35,
    0x1e, 0x24, 0x0e, 0x5e, 0x63, 0x58, 0xd1, 0xa2,
    0x25, 0x22, 0x7c, 0x3b, 0x01, 0x21, 0x78, 0x87,
    0xd4, 0x00, 0x46, 0x57, 0x9f, 0xd3, 0x27, 0x52,
    0x4c, 0x36, 0x02, 0xe7, 0xa0, 0xc4, 0xc8, 0x9e,
    0xea, 0xbf, 0x8a, 0xd2, 0x40, 0xc7, 0x38, 0xb5,
    0xa3, 0xf7, 0xf2, 0xce, 0xf9, 0x61, 0x15, 0xa1,
    0xe0, 0xae, 0x5d, 0xa4, 0x9b, 0x34, 0x1a, 0x55,
    0xad, 0x93, 0x32, 0x30, 0xf5, 0x8c, 0xb1, 0xe3,
    0x1d, 0xf6, 0xe2, 0x2e, 0x82, 0x66, 0xca, 0x60,
    0xc0, 0x29, 0x23, 0xab, 0x0d, 0x53, 0x4e, 0x6f,
    0xd5, 0xdb, 0x37, 0x45, 0xde, 0xfd, 0x8e, 0x2f,
    0x03, 0xff, 0x6a, 0x72, 0x6d, 0x6c, 0x5b, 0x51,
    0x8d, 0x1b, 0xaf, 0x92, 0xbb, 0xdd, 0xbc, 0x7f,
    0x11, 0xd9, 0x5c, 0x41, 0x1f, 0x10, 0x5a, 0xd8,
    0x0a, 0xc1, 0x31, 0x88, 0xa5, 0xcd, 0x7b, 0xbd,
    0x2d, 0x74, 0xd0, 0x12, 0xb8, 0xe5, 0xb4, 0xb0,
    0x89, 0x69, 0x97, 0x4a, 0x0c, 0x96, 0x77, 0x7e,
    0x65, 0xb9, 0xf1, 0x09, 0xc5, 0x6e, 0xc6, 0x84,
    0x18, 0xf0, 0x7d, 0xec, 0x3a, 0xdc, 0x4d, 0x20,
    0x79, 0xee, 0x5f, 0x3e, 0xd7, 0xcb, 0x39, 0x48,
};

// Widened S-box for _mm256_i32gather_epi32 (scale=4).
alignas(32) constexpr uint32_t SBOX_32[256] = {
    0xd6, 0x90, 0xe9, 0xfe, 0xcc, 0xe1, 0x3d, 0xb7,
    0x16, 0xb6, 0x14, 0xc2, 0x28, 0xfb, 0x2c, 0x05,
    0x2b, 0x67, 0x9a, 0x76, 0x2a, 0xbe, 0x04, 0xc3,
    0xaa, 0x44, 0x13, 0x26, 0x49, 0x86, 0x06, 0x99,
    0x9c, 0x42, 0x50, 0xf4, 0x91, 0xef, 0x98, 0x7a,
    0x33, 0x54, 0x0b, 0x43, 0xed, 0xcf, 0xac, 0x62,
    0xe4, 0xb3, 0x1c, 0xa9, 0xc9, 0x08, 0xe8, 0x95,
    0x80, 0xdf, 0x94, 0xfa, 0x75, 0x8f, 0x3f, 0xa6,
    0x47, 0x07, 0xa7, 0xfc, 0xf3, 0x73, 0x17, 0xba,
    0x83, 0x59, 0x3c, 0x19, 0xe6, 0x85, 0x4f, 0xa8,
    0x68, 0x6b, 0x81, 0xb2, 0x71, 0x64, 0xda, 0x8b,
    0xf8, 0xeb, 0x0f, 0x4b, 0x70, 0x56, 0x9d, 0x35,
    0x1e, 0x24, 0x0e, 0x5e, 0x63, 0x58, 0xd1, 0xa2,
    0x25, 0x22, 0x7c, 0x3b, 0x01, 0x21, 0x78, 0x87,
    0xd4, 0x00, 0x46, 0x57, 0x9f, 0xd3, 0x27, 0x52,
    0x4c, 0x36, 0x02, 0xe7, 0xa0, 0xc4, 0xc8, 0x9e,
    0xea, 0xbf, 0x8a, 0xd2, 0x40, 0xc7, 0x38, 0xb5,
    0xa3, 0xf7, 0xf2, 0xce, 0xf9, 0x61, 0x15, 0xa1,
    0xe0, 0xae, 0x5d, 0xa4, 0x9b, 0x34, 0x1a, 0x55,
    0xad, 0x93, 0x32, 0x30, 0xf5, 0x8c, 0xb1, 0xe3,
    0x1d, 0xf6, 0xe2, 0x2e, 0x82, 0x66, 0xca, 0x60,
    0xc0, 0x29, 0x23, 0xab, 0x0d, 0x53, 0x4e, 0x6f,
    0xd5, 0xdb, 0x37, 0x45, 0xde, 0xfd, 0x8e, 0x2f,
    0x03, 0xff, 0x6a, 0x72, 0x6d, 0x6c, 0x5b, 0x51,
    0x8d, 0x1b, 0xaf, 0x92, 0xbb, 0xdd, 0xbc, 0x7f,
    0x11, 0xd9, 0x5c, 0x41, 0x1f, 0x10, 0x5a, 0xd8,
    0x0a, 0xc1, 0x31, 0x88, 0xa5, 0xcd, 0x7b, 0xbd,
    0x2d, 0x74, 0xd0, 0x12, 0xb8, 0xe5, 0xb4, 0xb0,
    0x89, 0x69, 0x97, 0x4a, 0x0c, 0x96, 0x77, 0x7e,
    0x65, 0xb9, 0xf1, 0x09, 0xc5, 0x6e, 0xc6, 0x84,
    0x18, 0xf0, 0x7d, 0xec, 0x3a, 0xdc, 0x4d, 0x20,
    0x79, 0xee, 0x5f, 0x3e, 0xd7, 0xcb, 0x39, 0x48,
};

alignas(32) constexpr uint32_t FK[4] = {
    0xA3B1BAC6, 0x56AA3350, 0x677D9197, 0xB27022DC,
};

alignas(32) constexpr uint32_t CK[32] = {
    0x00070E15, 0x1C232A31, 0x383F464D, 0x545B6269,
    0x70777E85, 0x8C939AA1, 0xA8AFB6BD, 0xC4CBD2D9,
    0xE0E7EEF5, 0xFC030A11, 0x181F262D, 0x343B4249,
    0x50575E65, 0x6C737A81, 0x888F969D, 0xA4ABB2B9,
    0xC0C7CED5, 0xDCE3EAF1, 0xF8FF060D, 0x141B2229,
    0x30373E45, 0x4C535A61, 0x686F767D, 0x848B9299,
    0xA0A7AEB5, 0xBCC3CAD1, 0xD8DFE6ED, 0xF4FB0209,
    0x10171E25, 0x2C333A41, 0x484F565D, 0x646B7279,
};

inline uint32_t rotl(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

inline uint32_t sm4_sbox_word(uint32_t x) {
    return (static_cast<uint32_t>(SBOX[(x >> 24) & 0xFF]) << 24) |
           (static_cast<uint32_t>(SBOX[(x >> 16) & 0xFF]) << 16) |
           (static_cast<uint32_t>(SBOX[(x >> 8) & 0xFF]) << 8)  |
           (static_cast<uint32_t>(SBOX[x & 0xFF]));
}

inline uint32_t sm4_l_prime(uint32_t x) {
    return x ^ rotl(x, 13) ^ rotl(x, 23);
}

inline uint32_t sm4_t_prime(uint32_t x) {
    return sm4_l_prime(sm4_sbox_word(x));
}

void load_be32(const uint8_t src[4], uint32_t& dst) {
    dst = (static_cast<uint32_t>(src[0]) << 24) |
          (static_cast<uint32_t>(src[1]) << 16) |
          (static_cast<uint32_t>(src[2]) << 8)  |
          (static_cast<uint32_t>(src[3]));
}

// ---------------------------------------------------------------------------
// Phase B: counter generation helpers
// ---------------------------------------------------------------------------

// 4-way 64-bit byte-swap: reverses bytes within each 64-bit element.
// _mm256_shuffle_epi8 operates within 128-bit lanes.
alignas(32) constexpr uint8_t BSWAP64_MASK[32] = {
    7,  6,  5,  4,  3,  2,  1,  0,   // element 0 (bytes 0-7)
    15, 14, 13, 12, 11, 10, 9,  8,   // element 1 (bytes 8-15)
    7,  6,  5,  4,  3,  2,  1,  0,   // element 2 (bytes 16-23)
    15, 14, 13, 12, 11, 10, 9,  8,   // element 3 (bytes 24-31)
};

// 8-way 32-bit byte-swap: reverses bytes within each 32-bit element.
alignas(32) constexpr uint8_t BSWAP32_MASK[32] = {
    3,  2,  1,  0,   // element 0 (bytes 0-3)
    7,  6,  5,  4,   // element 1 (bytes 4-7)
    11, 10, 9,  8,   // element 2 (bytes 8-11)
    15, 14, 13, 12,  // element 3 (bytes 12-15)
    3,  2,  1,  0,   // element 4 (bytes 16-19)
    7,  6,  5,  4,   // element 5 (bytes 20-23)
    11, 10, 9,  8,   // element 6 (bytes 24-27)
    15, 14, 13, 12,  // element 7 (bytes 28-31)
};

inline uint64_t bswap64(uint64_t x) {
    return __builtin_bswap64(x);
}

// Verified big-endian 128-bit counter increment (byte-identical to SM4Standard).
inline void ctr_increment(uint8_t counter[16]) {
    for (int i = 15; i >= 0; --i) {
        if (++counter[i] != 0) break;
    }
}

// Transpose 16 consecutive BE counter blocks (temp[256], block-sliced)
// into word-sliced format (8 × __m256i in state_matrix).
//   state_matrix[batch*4 + word] = word 'word' of counters batch*8 .. batch*8+7
inline void transpose_to_word_sliced(const uint8_t temp[256],
                                     __m256i* state_matrix) {
    for (int batch = 0; batch < 2; ++batch) {
        for (int word = 0; word < 4; ++word) {
            alignas(32) int32_t indices[8];
            for (int k = 0; k < 8; ++k) {
                indices[k] = k * 4 + word;
            }
            __m256i idx = _mm256_load_si256(
                reinterpret_cast<const __m256i*>(indices));
            const int* base = reinterpret_cast<const int*>(temp + batch * 128);
            state_matrix[batch * 4 + word] =
                _mm256_i32gather_epi32(base, idx, 4);
        }
    }
}

// 8-way 32-bit rotate left: (x << n) | (x >> (32-n))
inline __m256i rotl32_avx2(__m256i x, int n) {
    return _mm256_or_si256(_mm256_slli_epi32(x, n),
                           _mm256_srli_epi32(x, 32 - n));
}

// Inverse of transpose_to_word_sliced: word-sliced (8 × __m256i)
// → 16 consecutive 16-byte BE keystream blocks.
inline void transpose_from_word_sliced(const __m256i* state,
                                       uint8_t keystream[256]) {
    alignas(32) uint32_t words[8][8];
    for (int i = 0; i < 8; ++i) {
        _mm256_store_si256(reinterpret_cast<__m256i*>(words[i]), state[i]);
    }
    for (int b = 0; b < 16; ++b) {
        int batch = b / 8;
        int idx   = b % 8;
        for (int w = 0; w < 4; ++w) {
            uint32_t val = words[batch * 4 + w][idx];
            uint8_t* dst = keystream + b * 16 + w * 4;
            dst[0] = static_cast<uint8_t>(val & 0xFF);
            dst[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
            dst[2] = static_cast<uint8_t>((val >> 16) & 0xFF);
            dst[3] = static_cast<uint8_t>((val >> 24) & 0xFF);
        }
    }
}

// Scalar SM4 block encrypt using pre-computed round keys (tail fallback).
inline void sm4_encrypt_block_scalar(const uint32_t rk[32],
                                     const uint8_t in[16],
                                     uint8_t out[16]) {
    uint32_t x[36];
    for (int i = 0; i < 4; ++i) {
        x[i] = (static_cast<uint32_t>(in[4 * i + 0]) << 24) |
               (static_cast<uint32_t>(in[4 * i + 1]) << 16) |
               (static_cast<uint32_t>(in[4 * i + 2]) << 8)  |
               (static_cast<uint32_t>(in[4 * i + 3]));
    }
    for (int i = 0; i < 32; ++i) {
        uint32_t t_sbox = sm4_sbox_word(x[i + 1] ^ x[i + 2] ^ x[i + 3] ^ rk[i]);
        uint32_t t_l = t_sbox ^ rotl(t_sbox, 2) ^ rotl(t_sbox, 10) ^
                       rotl(t_sbox, 18) ^ rotl(t_sbox, 24);
        x[i + 4] = x[i] ^ t_l;
    }
    // Reverse output: (X35, X34, X33, X32)
    for (int i = 0; i < 4; ++i) {
        uint32_t w = x[35 - i];
        out[4 * i + 0] = static_cast<uint8_t>((w >> 24) & 0xFF);
        out[4 * i + 1] = static_cast<uint8_t>((w >> 16) & 0xFF);
        out[4 * i + 2] = static_cast<uint8_t>((w >> 8) & 0xFF);
        out[4 * i + 3] = static_cast<uint8_t>(w & 0xFF);
    }
}

} // namespace

SM4AVX2::SM4AVX2(const std::vector<uint8_t>& key) {
    key_expansion(key.data());
}

SM4AVX2::~SM4AVX2() {
    volatile uint32_t* p = rk_;
    for (int i = 0; i < 32; ++i) {
        p[i] = 0;
    }
}

void SM4AVX2::key_expansion(const uint8_t key[16]) {
    uint32_t mk[4];
    for (int i = 0; i < 4; ++i) {
        load_be32(key + 4 * i, mk[i]);
    }

    uint32_t k[36];
    k[0] = mk[0] ^ FK[0];
    k[1] = mk[1] ^ FK[1];
    k[2] = mk[2] ^ FK[2];
    k[3] = mk[3] ^ FK[3];

    for (int i = 0; i < 32; ++i) {
        k[i + 4] = k[i] ^ sm4_t_prime(k[i + 1] ^ k[i + 2] ^ k[i + 3] ^ CK[i]);
        rk_[i] = k[i + 4];
    }
}

// ---------------------------------------------------------------------------
// Phase B Step A: AVX2 vectorized counter generation — GREEN
// Fast-Path (99.99%): AVX2 vector addition + BSWAP
// Slow-Path (overflow): scalar fallback
// ---------------------------------------------------------------------------

void SM4AVX2::generate_16_counters(__m256i* state_matrix,
                                   const uint8_t* initial_counter) {
    // ── 1. Extract high / low 64-bit halves (BE → LE) ──
    uint64_t hi_be, lo_be;
    std::memcpy(&hi_be, initial_counter, 8);
    std::memcpy(&lo_be, initial_counter + 8, 8);
    const uint64_t hi_le = bswap64(hi_be);
    const uint64_t lo_le = bswap64(lo_be);

    alignas(32) uint8_t temp[256];

    // ── 2. Overflow detection ──
    // lo_le near UINT64_MAX → 64-bit overflow; byte 15 > 0xF0 → LE↔BE
    // byte-boundary carry within 16 increments would break fast-path arithmetic.
    if (lo_le + 15 < lo_le || initial_counter[15] > 0xF0) {
        // ── Slow-Path: scalar loop ──
        uint8_t ctr[16];
        std::memcpy(ctr, initial_counter, 16);
        for (int i = 0; i < 16; ++i) {
            std::memcpy(temp + i * 16, ctr, 16);
            ctr_increment(ctr);
        }
    } else {
        // ── Fast-Path: AVX2 vectorized ──
        const __m256i bswap_mask =
            _mm256_load_si256(reinterpret_cast<const __m256i*>(BSWAP64_MASK));

        // base = [hi_le, lo_le, hi_le, lo_le] in memory order
        // _mm256_set_epi64x(e3, e2, e1, e0) → mem: [e0, e1, e2, e3]
        const __m256i base =
            _mm256_set_epi64x(lo_le, hi_le, lo_le, hi_le);

        for (int i = 0; i < 8; ++i) {
            // offset_i = [0, 2i, 0, 2i+1] in memory order
            const __m256i offset =
                _mm256_set_epi64x(2 * i + 1, 0, 2 * i, 0);
            const __m256i sum = _mm256_add_epi64(base, offset);
            const __m256i be = _mm256_shuffle_epi8(sum, bswap_mask);
            _mm256_store_si256(
                reinterpret_cast<__m256i*>(temp + 32 * i), be);
        }
    }

    // ── 3. Transpose block-sliced → word-sliced ──
    transpose_to_word_sliced(temp, state_matrix);
}

// ---------------------------------------------------------------------------
// Phase C Step A: AVX2 parallel S-box (τ transform) — GREEN
// Byte-slice → gather from SBOX_32 → shift → OR-combine
// ---------------------------------------------------------------------------

void SM4AVX2::parallel_sbox_16(__m256i* in_out_words) {
    const __m256i mask_ff = _mm256_set1_epi32(0xFF);

    for (int r = 0; r < 2; ++r) {
        __m256i x = in_out_words[r];

        // Byte-slice: extract each byte position as a gather index
        __m256i idx0 = _mm256_and_si256(x, mask_ff);
        __m256i idx1 = _mm256_and_si256(_mm256_srli_epi32(x, 8), mask_ff);
        __m256i idx2 = _mm256_and_si256(_mm256_srli_epi32(x, 16), mask_ff);
        __m256i idx3 = _mm256_srli_epi32(x, 24);

        // Parallel gather from widened S-box (scale=4 for uint32_t)
        __m256i t0 = _mm256_i32gather_epi32(reinterpret_cast<const int*>(SBOX_32), idx0, 4);
        __m256i t1 = _mm256_i32gather_epi32(reinterpret_cast<const int*>(SBOX_32), idx1, 4);
        __m256i t2 = _mm256_i32gather_epi32(reinterpret_cast<const int*>(SBOX_32), idx2, 4);
        __m256i t3 = _mm256_i32gather_epi32(reinterpret_cast<const int*>(SBOX_32), idx3, 4);

        // Shift each byte back to its original lane position and combine
        in_out_words[r] =
            _mm256_or_si256(t0,
            _mm256_or_si256(_mm256_slli_epi32(t1, 8),
            _mm256_or_si256(_mm256_slli_epi32(t2, 16),
                            _mm256_slli_epi32(t3, 24))));
    }
}

// ---------------------------------------------------------------------------
// Phase D: AVX2 parallel L transform (8-way SM4 linear layer)
// ---------------------------------------------------------------------------

__m256i SM4AVX2::parallel_L_transform(__m256i b) {
    __m256i t = _mm256_xor_si256(rotl32_avx2(b, 2), rotl32_avx2(b, 10));
    t = _mm256_xor_si256(t, rotl32_avx2(b, 18));
    t = _mm256_xor_si256(t, rotl32_avx2(b, 24));
    return _mm256_xor_si256(b, t);
}

// ---------------------------------------------------------------------------
// Phase D: Full 32-round SM4-CTR encryption — GREEN
// Pipeline: counters → BSWAP32(BE→LE) → 32-round SM4 → reversal →
//            BSWAP32(LE→BE) → inverse transpose → XOR plaintext
// ---------------------------------------------------------------------------

std::vector<uint8_t> SM4AVX2::encrypt_ctr(
    const std::vector<uint8_t>& iv,
    const std::vector<uint8_t>& plaintext) const {

    const size_t size = plaintext.size();
    std::vector<uint8_t> ciphertext(size);

    const __m256i bswap32_mask =
        _mm256_load_si256(reinterpret_cast<const __m256i*>(BSWAP32_MASK));

    // 32-byte-aligned working buffers
    uint8_t* src = static_cast<uint8_t*>(_mm_malloc(size + 32, 32));
    uint8_t* dst = static_cast<uint8_t*>(_mm_malloc(size + 32, 32));
    std::memcpy(src, plaintext.data(), size);

    // ── Counter state ──
    alignas(32) uint8_t ctr[16];
    std::memcpy(ctr, iv.data(), 16);

    size_t offset = 0;

    // ── Fast path: process full 256-byte chunks (16 blocks = 2 batches × 8) ──
    while (offset + 256 <= size) {
        // 1. Generate 16 consecutive BE counter blocks → word-sliced state
        alignas(32) uint8_t state_raw[256];
        auto* state = reinterpret_cast<__m256i*>(state_raw);
        generate_16_counters(state, ctr);

        // Advance IV counter by 16
        for (int i = 0; i < 16; ++i) ctr_increment(ctr);

        // 2. BSWAP32 each state register: BE bytes → LE native integers
        for (int i = 0; i < 8; ++i) {
            state[i] = _mm256_shuffle_epi8(state[i], bswap32_mask);
        }

        // 3. 32-round SM4: batch A (regs 0..3) + batch B (regs 4..7)
        __m256i r0 = state[0], r1 = state[1], r2 = state[2], r3 = state[3];
        __m256i r4 = state[4], r5 = state[5], r6 = state[6], r7 = state[7];

        __m256i sbox_in[2];

        for (int round = 0; round < 32; ++round) {
            __m256i rk_vec =
                _mm256_set1_epi32(static_cast<int>(rk_[round]));

            // T input: X[i+1] ⊕ X[i+2] ⊕ X[i+3] ⊕ rk
            __m256i t_in_a =
                _mm256_xor_si256(_mm256_xor_si256(r1, r2),
                                 _mm256_xor_si256(r3, rk_vec));
            __m256i t_in_b =
                _mm256_xor_si256(_mm256_xor_si256(r5, r6),
                                 _mm256_xor_si256(r7, rk_vec));

            // τ (16-way parallel S-box)
            sbox_in[0] = t_in_a;
            sbox_in[1] = t_in_b;
            parallel_sbox_16(sbox_in);

            // L transform on each batch
            __m256i t_out_a = parallel_L_transform(sbox_in[0]);
            __m256i t_out_b = parallel_L_transform(sbox_in[1]);

            // X[i+4] = X[i] ⊕ T(...)
            __m256i r0_new = _mm256_xor_si256(r0, t_out_a);
            __m256i r4_new = _mm256_xor_si256(r4, t_out_b);

            // Slide register window
            r0 = r1; r1 = r2; r2 = r3; r3 = r0_new;
            r4 = r5; r5 = r6; r6 = r7; r7 = r4_new;
        }

        // 4. Output reversal: (X35, X34, X33, X32) per batch
        state[0] = r3; state[1] = r2; state[2] = r1; state[3] = r0;
        state[4] = r7; state[5] = r6; state[6] = r5; state[7] = r4;

        // 5. BSWAP32: LE native → BE keystream bytes
        for (int i = 0; i < 8; ++i) {
            state[i] = _mm256_shuffle_epi8(state[i], bswap32_mask);
        }

        // 6. Inverse transpose → 16 consecutive BE keystream blocks
        alignas(32) uint8_t keystream[256];
        transpose_from_word_sliced(state, keystream);

        // 7. XOR keystream with plaintext (8 × 32-byte loads)
        for (int i = 0; i < 8; ++i) {
            __m256i ks = _mm256_load_si256(
                reinterpret_cast<const __m256i*>(keystream + i * 32));
            __m256i pt_blk = _mm256_load_si256(
                reinterpret_cast<const __m256i*>(src + offset + i * 32));
            _mm256_store_si256(
                reinterpret_cast<__m256i*>(dst + offset + i * 32),
                _mm256_xor_si256(pt_blk, ks));
        }

        offset += 256;
    }

    // ── Tail: < 256 bytes — scalar fallback ──
    while (offset < size) {
        uint8_t keystream[16];
        sm4_encrypt_block_scalar(rk_, ctr, keystream);

        size_t chunk = size - offset;
        if (chunk > 16) chunk = 16;
        for (size_t i = 0; i < chunk; ++i) {
            dst[offset + i] = src[offset + i] ^ keystream[i];
        }
        offset += chunk;
        ctr_increment(ctr);
    }

    std::memcpy(ciphertext.data(), dst, size);

    _mm_free(src);
    _mm_free(dst);
    return ciphertext;
}
