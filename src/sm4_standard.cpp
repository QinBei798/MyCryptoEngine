#include "sm4_standard.h"

#include <cstring>

namespace {

// GM/T 0002-2012 S-box
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

inline uint32_t sm4_l(uint32_t x) {
    return x ^ rotl(x, 2) ^ rotl(x, 10) ^ rotl(x, 18) ^ rotl(x, 24);
}

inline uint32_t sm4_l_prime(uint32_t x) {
    return x ^ rotl(x, 13) ^ rotl(x, 23);
}

inline uint32_t sm4_t(uint32_t x) {
    return sm4_l(sm4_sbox_word(x));
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

void store_be32(uint32_t src, uint8_t dst[4]) {
    dst[0] = static_cast<uint8_t>((src >> 24) & 0xFF);
    dst[1] = static_cast<uint8_t>((src >> 16) & 0xFF);
    dst[2] = static_cast<uint8_t>((src >> 8) & 0xFF);
    dst[3] = static_cast<uint8_t>(src & 0xFF);
}

} // namespace

SM4Standard::SM4Standard(const std::vector<uint8_t>& key) {
    key_expansion(key.data());
}

SM4Standard::~SM4Standard() {
    // Secure wipe — volatile prevents -O3 from eliding the zeroing stores
    volatile uint32_t* p = rk_;
    for (int i = 0; i < 32; ++i) {
        p[i] = 0;
    }
}

void SM4Standard::key_expansion(const uint8_t key[16]) {
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

void SM4Standard::encrypt_block(const uint8_t plaintext[16],
                                uint8_t ciphertext[16]) const {
    uint32_t x[36];
    for (int i = 0; i < 4; ++i) {
        load_be32(plaintext + 4 * i, x[i]);
    }

    for (int i = 0; i < 32; ++i) {
        x[i + 4] = x[i] ^ sm4_t(x[i + 1] ^ x[i + 2] ^ x[i + 3] ^ rk_[i]);
    }

    // Output: reverse order (X35, X34, X33, X32)
    for (int i = 0; i < 4; ++i) {
        store_be32(x[35 - i], ciphertext + 4 * i);
    }
}

void SM4Standard::decrypt_block(const uint8_t ciphertext[16],
                                uint8_t plaintext[16]) const {
    uint32_t x[36];
    for (int i = 0; i < 4; ++i) {
        load_be32(ciphertext + 4 * i, x[i]);
    }

    // Reverse round key order
    for (int i = 0; i < 32; ++i) {
        x[i + 4] = x[i] ^ sm4_t(x[i + 1] ^ x[i + 2] ^ x[i + 3] ^ rk_[31 - i]);
    }

    for (int i = 0; i < 4; ++i) {
        store_be32(x[35 - i], plaintext + 4 * i);
    }
}

namespace {

// Big-endian increment of a 16-byte counter block.
// Starting from byte[15] (LSB), increment with carry propagation.
inline void ctr_increment(uint8_t counter[16]) {
    for (int i = 15; i >= 0; --i) {
        if (++counter[i] != 0) break;
    }
}

} // namespace

std::vector<uint8_t> SM4Standard::encrypt_ctr(
    const std::vector<uint8_t>& iv,
    const std::vector<uint8_t>& plaintext) const {

    std::vector<uint8_t> ciphertext(plaintext.size());
    uint8_t counter[16];
    std::memcpy(counter, iv.data(), 16);

    const size_t full_blocks = plaintext.size() / 16;
    const size_t tail = plaintext.size() % 16;

    for (size_t b = 0; b < full_blocks; ++b) {
        uint8_t keystream[16];
        encrypt_block(counter, keystream);

        const uint8_t* pt = plaintext.data() + b * 16;
        uint8_t* ct = ciphertext.data() + b * 16;
        for (int i = 0; i < 16; ++i) {
            ct[i] = pt[i] ^ keystream[i];
        }

        ctr_increment(counter);
    }

    if (tail > 0) {
        uint8_t keystream[16];
        encrypt_block(counter, keystream);

        const uint8_t* pt = plaintext.data() + full_blocks * 16;
        uint8_t* ct = ciphertext.data() + full_blocks * 16;
        for (size_t i = 0; i < tail; ++i) {
            ct[i] = pt[i] ^ keystream[i];
        }
    }

    return ciphertext;
}
