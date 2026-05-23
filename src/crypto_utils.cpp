#include "crypto_utils.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/kdf.h>

void derive_keys(const std::string& password,
                 const std::vector<uint8_t>& salt,
                 std::vector<uint8_t>& out_sm4_key,
                 std::vector<uint8_t>& out_hmac_key) {
    constexpr int iterations = 100000;
    constexpr int total_len = 48; // 16 (SM4) + 32 (HMAC-SM3)
    std::vector<uint8_t> derived(total_len);

    PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
                      salt.data(), static_cast<int>(salt.size()),
                      iterations,
                      EVP_sm3(),
                      total_len, derived.data());

    out_sm4_key.assign(derived.begin(), derived.begin() + 16);
    out_hmac_key.assign(derived.begin() + 16, derived.end());
}

std::vector<uint8_t> calc_hmac_sm3(const std::vector<uint8_t>& hmac_key,
                                   const std::vector<uint8_t>& data) {
    std::vector<uint8_t> result(32); // SM3 digest is 32 bytes
    unsigned int md_len = 0;

    HMAC(EVP_sm3(),
         hmac_key.data(), static_cast<int>(hmac_key.size()),
         data.data(), data.size(),
         result.data(), &md_len);

    result.resize(md_len);
    return result;
}
