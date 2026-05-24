#pragma once

#include <cstdint>
#include <vector>

class SecureChunkPipeline {
public:
    SecureChunkPipeline();
    ~SecureChunkPipeline();

    // Non-copyable, movable
    SecureChunkPipeline(const SecureChunkPipeline&) = delete;
    SecureChunkPipeline& operator=(const SecureChunkPipeline&) = delete;
    SecureChunkPipeline(SecureChunkPipeline&&) = default;
    SecureChunkPipeline& operator=(SecureChunkPipeline&&) = default;

    // Seal: compress -> encrypt -> sign
    // Returns ciphertext || HMAC-SM3 (32 bytes appended)
    // out_original_size is set to plaintext.size() for later decompression
    std::vector<uint8_t> seal_chunk(const std::vector<uint8_t>& plaintext,
                                    const std::vector<uint8_t>& iv,
                                    const std::vector<uint8_t>& sm4_key,
                                    const std::vector<uint8_t>& hmac_key,
                                    uint64_t& out_original_size);

    // Open: verify HMAC -> decrypt -> decompress
    // ciphertext_with_mac: ciphertext || HMAC-SM3 (32 bytes at end)
    // original_size: the uncompressed size recorded during seal
    std::vector<uint8_t> open_chunk(const std::vector<uint8_t>& ciphertext_with_mac,
                                    const std::vector<uint8_t>& iv,
                                    const std::vector<uint8_t>& sm4_key,
                                    const std::vector<uint8_t>& hmac_key,
                                    uint64_t original_size);
};
