#include "secure_chunk_pipeline.h"
#include <gtest/gtest.h>
#include <random>
#include <cstdint>
#include <vector>

namespace {

std::vector<uint8_t> random_bytes(size_t n) {
    std::vector<uint8_t> bytes(n);
    std::random_device rd;
    std::mt19937_64 rng(rd());
    std::uniform_int_distribution<int> dist(0, 255);
    for (size_t i = 0; i < n; ++i)
        bytes[i] = static_cast<uint8_t>(dist(rng));
    return bytes;
}

// Test 1: Valid round-trip
TEST(SecureChunkPipelineTest, SealAndOpenRoundTrip) {
    SecureChunkPipeline pipeline;
    auto plaintext = random_bytes(1024);
    auto iv = random_bytes(16);
    auto sm4_key = random_bytes(16);
    auto hmac_key = random_bytes(32);

    uint64_t original_size = 0;
    auto sealed = pipeline.seal_chunk(plaintext, iv, sm4_key, hmac_key, original_size);

    // RED: stub returns empty — these assertions will fail
    EXPECT_GT(sealed.size(), 32u) << "seal_chunk must return ciphertext + 32B HMAC";
    EXPECT_EQ(original_size, 1024u) << "original_size must be recorded";

    auto recovered = pipeline.open_chunk(sealed, iv, sm4_key, hmac_key, original_size);

    // RED: stub returns empty — this assertion will fail
    EXPECT_EQ(recovered, plaintext) << "round-trip must restore original bytes";
}

// Test 2: Tamper detection must throw/abort
TEST(SecureChunkPipelineTest, TamperDetectionAndInterception) {
    SecureChunkPipeline pipeline;
    auto plaintext = random_bytes(512);
    auto iv = random_bytes(16);
    auto sm4_key = random_bytes(16);
    auto hmac_key = random_bytes(32);

    uint64_t original_size = 0;
    auto sealed = pipeline.seal_chunk(plaintext, iv, sm4_key, hmac_key, original_size);

    // If seal returned empty (RED stub), skip tampering — the test still fails
    if (sealed.size() > 32) {
        // Flip a bit in the ciphertext portion (before the last 32 bytes = HMAC)
        sealed[0] ^= 0x01;
    }

    // RED: stub open_chunk returns empty without throwing
    // This EXPECT_THROW will FAIL because stub doesn't throw
    EXPECT_THROW({
        pipeline.open_chunk(sealed, iv, sm4_key, hmac_key, original_size);
    }, std::runtime_error) << "tampered data must throw runtime_error";
}

// Test 3: Empty input boundary — must not crash and must produce valid output
TEST(SecureChunkPipelineTest, EmptyInputBoundary) {
    SecureChunkPipeline pipeline;
    std::vector<uint8_t> empty_plaintext;
    auto iv = random_bytes(16);
    auto sm4_key = random_bytes(16);
    auto hmac_key = random_bytes(32);

    uint64_t original_size = 0;
    // Should not crash
    auto sealed = pipeline.seal_chunk(empty_plaintext, iv, sm4_key, hmac_key, original_size);

    // RED: stub returns empty, but real impl must return at minimum the HMAC (32 bytes)
    // and the zstd frame header for empty input
    EXPECT_GT(sealed.size(), 32u) << "even empty input must produce HMAC + zstd frame > 32 bytes";
    EXPECT_EQ(original_size, 0u) << "original_size must be 0 for empty input";
}

} // namespace
