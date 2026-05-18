#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "sm4_standard.h"

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

static std::vector<uint8_t> hex_to_bytes(const char* hex) {
    std::vector<uint8_t> out;
    out.reserve(std::strlen(hex) / 2);
    for (size_t i = 0; hex[i] && hex[i + 1]; i += 2) {
        char buf[3] = {hex[i], hex[i + 1], '\0'};
        out.push_back(static_cast<uint8_t>(strtoul(buf, nullptr, 16)));
    }
    return out;
}

// ---------------------------------------------------------------------------
// GM/T 0002-2012 single-block (ECB) test vectors
// These verify S-box correctness AND key-expansion correctness.
// ---------------------------------------------------------------------------

struct SM4BlockTestVector {
    std::vector<uint8_t> key;
    std::vector<uint8_t> plaintext;
    std::vector<uint8_t> ciphertext;
    const char* description;
};

const SM4BlockTestVector kBlockVectors[] = {
    {
        // GM/T 0002-2012 Appendix A.1 — standard test vector
        hex_to_bytes("0123456789ABCDEFFEDCBA9876543210"),
        hex_to_bytes("0123456789ABCDEFFEDCBA9876543210"),
        hex_to_bytes("681EDF34D206965E86B3E94F536E4246"),
        "GM/T 0002-2012 standard single-block encrypt",
    },
    {
        // All-zero key → all-zero plaintext (smoke test)
        hex_to_bytes("00000000000000000000000000000000"),
        hex_to_bytes("00000000000000000000000000000000"),
        // Verified against reference SM4 implementation
        hex_to_bytes("9F1F7BFF6F5511384D9430531E538FD3"),
        "SM4 all-zero key, all-zero plaintext",
    },
};

// ---------------------------------------------------------------------------
// CTR test vectors (Phase B — still red until CTR is wired up)
// ---------------------------------------------------------------------------

struct SM4CTRTestVector {
    std::vector<uint8_t> key;
    std::vector<uint8_t> iv;
    std::vector<uint8_t> plaintext;
    std::vector<uint8_t> ciphertext;
    const char* description;
};

const SM4CTRTestVector kCTRVectors[] = {
    {
        hex_to_bytes("0123456789ABCDEFFEDCBA9876543210"),
        hex_to_bytes("00000000000000000000000000000000"),
        hex_to_bytes("0123456789ABCDEFFEDCBA9876543210"),
        hex_to_bytes("2754B10C806AEF23698989882D80903A"),
        "SM4-CTR single block (16 bytes)"
    },
    {
        hex_to_bytes("0123456789ABCDEFFEDCBA9876543210"),
        hex_to_bytes("00000000000000000000000000000000"),
        hex_to_bytes("0123456789ABCDEFFEDCBA9876543210"
                     "0123456789ABCDEFFEDCBA9876543210"),
        hex_to_bytes("2754B10C806AEF23698989882D80903A"
                     "4F7A1E97B68870FFCC4715CEEEBCAAFC"),
        "SM4-CTR two full blocks (32 bytes)"
    },
    {
        hex_to_bytes("0123456789ABCDEFFEDCBA9876543210"),
        hex_to_bytes("00000000000000000000000000000000"),
        hex_to_bytes("0123456789ABCDEFFEDCBA9876543210"
                     "01234567"),
        hex_to_bytes("2754B10C806AEF23698989882D80903A"
                     "4F7A1E97"),
        "SM4-CTR partial final block (20 bytes)"
    },
};

// ===================================================================
// GREEN — single-block encrypt / decrypt (Phase A)
// ===================================================================

class SM4BlockTest : public ::testing::TestWithParam<SM4BlockTestVector> {};

TEST_P(SM4BlockTest, EncryptBlockMatchesGMVector) {
    const auto& tv = GetParam();
    ASSERT_EQ(tv.key.size(), 16u);
    ASSERT_EQ(tv.plaintext.size(), 16u);
    ASSERT_EQ(tv.ciphertext.size(), 16u);

    SM4Standard cipher(tv.key);

    uint8_t out[16] = {};
    cipher.encrypt_block(tv.plaintext.data(), out);

    std::vector<uint8_t> result(out, out + 16);
    EXPECT_EQ(result, tv.ciphertext)
        << "Test vector failure: " << tv.description;
}

TEST_P(SM4BlockTest, DecryptBlockReversesEncrypt) {
    const auto& tv = GetParam();
    SM4Standard cipher(tv.key);

    uint8_t enc[16] = {};
    cipher.encrypt_block(tv.plaintext.data(), enc);

    uint8_t dec[16] = {};
    cipher.decrypt_block(enc, dec);

    std::vector<uint8_t> recovered(dec, dec + 16);
    EXPECT_EQ(recovered, tv.plaintext)
        << "decrypt_block did not reverse encrypt_block: " << tv.description;
}

TEST_P(SM4BlockTest, DecryptBlockOfKnownCiphertext) {
    const auto& tv = GetParam();
    SM4Standard cipher(tv.key);

    uint8_t dec[16] = {};
    cipher.decrypt_block(tv.ciphertext.data(), dec);

    std::vector<uint8_t> result(dec, dec + 16);
    EXPECT_EQ(result, tv.plaintext)
        << "decrypt_block of known ciphertext failed: " << tv.description;
}

INSTANTIATE_TEST_SUITE_P(
    SM4BlockVectors,
    SM4BlockTest,
    ::testing::ValuesIn(kBlockVectors),
    [](const ::testing::TestParamInfo<SM4BlockTestVector>& info) {
        std::string desc = info.param.description;
        std::string name;
        for (char c : desc) {
            if (isalnum(static_cast<unsigned char>(c))) name += c;
            else name += '_';
        }
        return name;
    });

// ===================================================================
// RED — CTR mode (Phase B — still failing until CTR is implemented)
// ===================================================================

class SM4CTRTest : public ::testing::TestWithParam<SM4CTRTestVector> {};

TEST_P(SM4CTRTest, EncryptCTRMatchesKnownVector) {
    const auto& tv = GetParam();
    SM4Standard cipher(tv.key);
    auto result = cipher.encrypt_ctr(tv.iv, tv.plaintext);
    EXPECT_EQ(result, tv.ciphertext)
        << "Test vector failure: " << tv.description;
}

INSTANTIATE_TEST_SUITE_P(
    SM4CTRVectors,
    SM4CTRTest,
    ::testing::ValuesIn(kCTRVectors),
    [](const ::testing::TestParamInfo<SM4CTRTestVector>& info) {
        std::string desc = info.param.description;
        std::string name;
        for (char c : desc) {
            if (isalnum(static_cast<unsigned char>(c))) name += c;
            else name += '_';
        }
        return name;
    });
