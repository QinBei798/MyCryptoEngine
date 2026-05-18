#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

class SM4Standard {
public:
    explicit SM4Standard(const std::vector<uint8_t>& key);
    ~SM4Standard();

    SM4Standard(const SM4Standard&) = delete;
    SM4Standard& operator=(const SM4Standard&) = delete;
    SM4Standard(SM4Standard&&) = default;
    SM4Standard& operator=(SM4Standard&&) = default;

    void encrypt_block(const uint8_t plaintext[16],
                       uint8_t ciphertext[16]) const;

    void decrypt_block(const uint8_t ciphertext[16],
                       uint8_t plaintext[16]) const;

    std::vector<uint8_t> encrypt_ctr(const std::vector<uint8_t>& iv,
                                     const std::vector<uint8_t>& plaintext) const;

private:
    void key_expansion(const uint8_t key[16]);

    alignas(32) uint32_t rk_[32];
};
