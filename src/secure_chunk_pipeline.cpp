#include "secure_chunk_pipeline.h"

#include <openssl/crypto.h>

#include <cstring>
#include <stdexcept>

#include "compress_utils.h"
#include "crypto_utils.h"
#include "sm4_avx2.h"

SecureChunkPipeline::SecureChunkPipeline() {}
SecureChunkPipeline::~SecureChunkPipeline() {}

std::vector<uint8_t> SecureChunkPipeline::seal_chunk(
    const std::vector<uint8_t>& plaintext,
    const std::vector<uint8_t>& iv,
    const std::vector<uint8_t>& sm4_key,
    const std::vector<uint8_t>& hmac_key,
    uint64_t& out_original_size)
{
    // Step 1: compress
    std::vector<uint8_t> compressed = compress_zstd(plaintext);

    // Step 2: encrypt (CTR mode)
    SM4AVX2 cipher(sm4_key);
    std::vector<uint8_t> ciphertext = cipher.encrypt_ctr(iv, compressed);

    // Wipe compressed plaintext after encryption
    OPENSSL_cleanse(compressed.data(), compressed.size());

    // Step 3: sign with HMAC-SM3
    std::vector<uint8_t> mac = calc_hmac_sm3(hmac_key, ciphertext);

    // Record original size for decompression
    out_original_size = plaintext.size();

    // Step 4: assemble ciphertext || HMAC
    std::vector<uint8_t> result;
    result.reserve(ciphertext.size() + mac.size());
    result.insert(result.end(), ciphertext.begin(), ciphertext.end());
    result.insert(result.end(), mac.begin(), mac.end());

    // Wipe ciphertext after result is assembled
    OPENSSL_cleanse(ciphertext.data(), ciphertext.size());

    return result;
}

std::vector<uint8_t> SecureChunkPipeline::open_chunk(
    const std::vector<uint8_t>& ciphertext_with_mac,
    const std::vector<uint8_t>& iv,
    const std::vector<uint8_t>& sm4_key,
    const std::vector<uint8_t>& hmac_key,
    uint64_t original_size)
{
    constexpr size_t kHmacSize = 32;

    if (ciphertext_with_mac.size() < kHmacSize) {
        throw std::runtime_error("open_chunk: input too short, missing HMAC");
    }

    // Step 1: split ciphertext and HMAC
    size_t ciphertext_len = ciphertext_with_mac.size() - kHmacSize;
    const uint8_t* data = ciphertext_with_mac.data();

    // Step 2: verify HMAC before any decryption
    std::vector<uint8_t> received_mac(data + ciphertext_len, data + ciphertext_with_mac.size());
    std::vector<uint8_t> expected_mac = calc_hmac_sm3(hmac_key,
        std::vector<uint8_t>(data, data + ciphertext_len));

    const bool hmac_ok = (CRYPTO_memcmp(received_mac.data(), expected_mac.data(), kHmacSize) == 0);
    OPENSSL_cleanse(received_mac.data(), received_mac.size());
    OPENSSL_cleanse(expected_mac.data(), expected_mac.size());

    if (!hmac_ok) {
        throw std::runtime_error("open_chunk: HMAC verification failed");
    }

    // Step 3: decrypt (CTR mode is symmetric — encrypt_ctr works for both directions)
    SM4AVX2 cipher(sm4_key);
    std::vector<uint8_t> ciphertext(data, data + ciphertext_len);
    std::vector<uint8_t> compressed = cipher.encrypt_ctr(iv, ciphertext);

    // Wipe ciphertext after decryption
    OPENSSL_cleanse(ciphertext.data(), ciphertext.size());

    // Step 4: decompress
    std::vector<uint8_t> plaintext = decompress_zstd(compressed, static_cast<size_t>(original_size));

    // Wipe compressed data after decompression
    OPENSSL_cleanse(compressed.data(), compressed.size());

    return plaintext;
}
