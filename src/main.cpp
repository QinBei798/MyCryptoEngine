#include "sm4_standard.h"
#include "sm4_avx2.h"
#include "crypto_utils.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <immintrin.h>
#include <openssl/crypto.h>

// ═══════════════════════════════════════════════════════════════════════
//  Utility helpers
// ═══════════════════════════════════════════════════════════════════════

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

void print_hex_line(const char* label, const std::vector<uint8_t>& data) {
    std::cout << std::hex << std::setfill('0');
    std::cout << label;
    for (size_t i = 0; i < data.size(); ++i) {
        if (i > 0 && i % 8 == 0)
            std::cout << " ";
        std::cout << std::setw(2) << static_cast<int>(data[i]) << " ";
    }
    std::cout << std::dec << "\n";
}

void print_hex_dump(const std::vector<uint8_t>& data) {
    constexpr int cols = 16;
    std::cout << std::hex << std::setfill('0');
    for (size_t i = 0; i < data.size(); i += cols) {
        std::cout << "  " << std::setw(6) << i << "  ";
        for (int j = 0; j < cols; ++j) {
            if (j == 8) std::cout << " ";
            if (i + j < data.size())
                std::cout << std::setw(2) << static_cast<int>(data[i + j]) << " ";
            else
                std::cout << "   ";
        }
        std::cout << " |";
        for (int j = 0; j < cols && (i + j) < data.size(); ++j) {
            uint8_t c = data[i + j];
            std::cout << (c >= 32 && c < 127 ? static_cast<char>(c) : '.');
        }
        std::cout << "|\n";
    }
    std::cout << std::dec;
}

// ── Robust hex parser ─────────────────────────────────────────────────
// Accepts: "0a f3 2c", "0af32c", "0A F3 2C", "0x0a 0xf3", "0a:f3:2c"
// Strips all non-hex characters, validates even length and hex charset.

std::vector<uint8_t> parse_hex(const std::string& input) {
    std::string clean;
    clean.reserve(input.size());
    for (char c : input) {
        if ((c >= '0' && c <= '9') ||
            (c >= 'a' && c <= 'f') ||
            (c >= 'A' && c <= 'F'))
            clean.push_back(c);
    }

    if (clean.empty())
        return {};

    if (clean.size() % 2 != 0) {
        std::cerr << "  [✗] Error: Hex string has odd number of nibbles ("
                  << clean.size() << ").\n";
        return {};
    }

    std::vector<uint8_t> bytes;
    bytes.reserve(clean.size() / 2);
    for (size_t i = 0; i < clean.size(); i += 2) {
        unsigned int byte;
        std::istringstream iss(clean.substr(i, 2));
        if (!(iss >> std::hex >> byte) || byte > 0xFF) {
            std::cerr << "  [✗] Error: Invalid hex byte at position " << i
                      << ": '" << clean.substr(i, 2) << "'\n";
            return {};
        }
        bytes.push_back(static_cast<uint8_t>(byte));
    }
    return bytes;
}

// ── Visual dividers ────────────────────────────────────────────────────

void thin_line() {
    for (int i = 0; i < 68; ++i) std::cout << "─";
    std::cout << "\n";
}

void thick_sep(const char* title) {
    std::cout << "\n╔";
    for (int i = 0; i < 66; ++i) std::cout << "═";
    std::cout << "╗\n";
    std::cout << "║  " << title;
    size_t len = std::strlen(title);
    for (int i = 0; i < 66 - static_cast<int>(len) - 3; ++i)
        std::cout << " ";
    std::cout << "║\n";
    std::cout << "╚";
    for (int i = 0; i < 66; ++i) std::cout << "═";
    std::cout << "╝\n\n";
}

void banner() {
    std::cout << R"(
  ╔══════════════════════════════════════════════════════════════════════╗
  ║                                                                      ║
  ║     ███╗   ███╗██╗   ██╗ ██████╗██████╗ ██╗   ██╗██████╗            ║
  ║     ████╗ ████║╚██╗ ██╔╝██╔════╝██╔══██╗╚██╗ ██╔╝██╔══██╗           ║
  ║     ██╔████╔██║ ╚████╔╝ ██║     ██████╔╝ ╚████╔╝ ██████╔╝           ║
  ║     ██║╚██╔╝██║  ╚██╔╝  ██║     ██╔══██╗  ╚██╔╝  ██╔═══╝            ║
  ║     ██║ ╚═╝ ██║   ██║   ╚██████╗██║  ██║   ██║   ██║                ║
  ║     ╚═╝     ╚═╝   ╚═╝    ╚═════╝╚═╝  ╚═╝   ╚═╝   ╚═╝                ║
  ║                                                                      ║
  ║        Interactive SM4-CTR Console · AVX2 Intrinsics · 2026          ║
  ║                                                                      ║
  ╚══════════════════════════════════════════════════════════════════════╝
)";
}

void print_menu() {
    std::cout << "\n";
    thin_line();
    std::cout << "  [1]  Encrypt — custom plaintext string\n";
    std::cout << "  [2]  Decrypt — hex ciphertext back to plaintext\n";
    std::cout << "  [3]  Benchmark — SM4Standard vs SM4AVX2 live speedrun\n";
    std::cout << "  [4]  Exit\n";
    std::cout << "  [5]  Encrypt File — secure storage to .sm4x container\n";
    std::cout << "  [6]  Decrypt File — extract from .sm4x container\n";
    thin_line();
    std::cout << "  Choice > " << std::flush;
}

// ── Menu handler: Encrypt string ──────────────────────────────────────

void do_encrypt(const SM4AVX2& engine, const std::vector<uint8_t>& iv) {
    std::cout << "\n  Enter plaintext (supports spaces, empty line = cancel):\n  > " << std::flush;

    std::string input;
    if (!std::getline(std::cin, input) || input.empty()) {
        std::cout << "  [!] Cancelled.\n";
        return;
    }

    std::vector<uint8_t> plaintext(input.begin(), input.end());
    auto ciphertext = engine.encrypt_ctr(iv, plaintext);

    std::cout << "\n  [+] Plaintext   (" << plaintext.size()
              << " bytes): \"" << input << "\"\n\n";
    std::cout << "  [+] CIPHERTEXT HEX DUMP  (" << ciphertext.size()
              << " bytes):\n\n";
    print_hex_dump(ciphertext);

    // Verify round-trip silently
    auto decrypted = engine.encrypt_ctr(iv, ciphertext);
    std::string recovered(decrypted.begin(), decrypted.end());
    std::cout << "\n  [✓] Round-trip verified: "
              << (recovered == input ? "OK" : "FAILED") << "\n";
}

// ── Menu handler: Decrypt hex ─────────────────────────────────────────

void do_decrypt(const SM4AVX2& engine, const std::vector<uint8_t>& iv) {
    std::cout << "\n  Enter hex ciphertext (spaces/colons/0x ignored, empty line = cancel):\n"
              << "  Example: 2e 21 a6 e1 75 c1 a0 45 ...\n  > " << std::flush;

    std::string input;
    if (!std::getline(std::cin, input) || input.empty()) {
        std::cout << "  [!] Cancelled.\n";
        return;
    }

    auto ciphertext = parse_hex(input);
    if (ciphertext.empty()) {
        std::cout << "  [!] No valid hex data parsed.\n";
        return;
    }

    std::cout << "  [+] Parsed " << ciphertext.size() << " bytes of ciphertext.\n";

    auto plaintext = engine.encrypt_ctr(iv, ciphertext);
    std::string recovered(plaintext.begin(), plaintext.end());

    bool is_printable = std::all_of(plaintext.begin(), plaintext.end(), [](uint8_t c) {
        return c >= 32 && c < 127;
    });

    if (is_printable) {
        std::cout << "\n  [+] Decrypted Plaintext:\n"
                  << "  ┌─────────────────────────────────────────────────────────────┐\n"
                  << "  │ " << recovered << "\n"
                  << "  └─────────────────────────────────────────────────────────────┘\n";
    } else {
        std::cout << "\n  [+] Decrypted (binary output, " << plaintext.size()
                  << " bytes):\n\n";
        print_hex_dump(plaintext);
    }
}

// ── Menu handler: Benchmark ───────────────────────────────────────────

void do_benchmark(const std::vector<uint8_t>& key,
                  const std::vector<uint8_t>& iv) {
    std::cout << "\n  Enter data size in MB (e.g. 10, 100, 1000, empty line = cancel):\n  > " << std::flush;

    std::string input;
    if (!std::getline(std::cin, input) || input.empty()) {
        std::cout << "  [!] Cancelled.\n";
        return;
    }

    int size_mb = 0;
    try {
        size_mb = std::stoi(input);
    } catch (...) {
        std::cout << "  [✗] Invalid number.\n";
        return;
    }

    if (size_mb <= 0 || size_mb > 4096) {
        std::cout << "  [✗] Size must be between 1 and 4096 MB.\n";
        return;
    }

    size_t total_bytes = static_cast<size_t>(size_mb) * 1024ULL * 1024ULL;

    std::cout << "\n  [+] Allocating " << size_mb
              << " MB of 32-byte-aligned random data...\n";

    uint8_t* raw_buf = static_cast<uint8_t*>(_mm_malloc(total_bytes, 32));
    if (!raw_buf) {
        std::cout << "  [✗] _mm_malloc failed — not enough memory?\n";
        return;
    }

    // Fill with PRNG data (deterministic seed for reproducibility)
    {
        std::mt19937_64 rng(0xCAFE0420DEADBEEFULL);
        std::uniform_int_distribution<int> dist(0, 255);
        for (size_t i = 0; i < total_bytes; ++i)
            raw_buf[i] = static_cast<uint8_t>(dist(rng));
    }

    std::vector<uint8_t> pt(raw_buf, raw_buf + total_bytes);

    // ── Scalar benchmark ──────────────────────────────────────────
    std::cout << "  [+] Running SM4Standard (scalar)  ... " << std::flush;
    SM4Standard std_engine(key);
    auto t1 = std::chrono::high_resolution_clock::now();
    auto ct_standard = std_engine.encrypt_ctr(iv, pt);
    auto t2 = std::chrono::high_resolution_clock::now();
    auto ms_standard = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
    std::cout << "done.\n";

    // ── AVX2 benchmark ────────────────────────────────────────────
    std::cout << "  [+] Running SM4AVX2     (SIMD)   ... " << std::flush;
    SM4AVX2 avx2_engine(key);
    auto t3 = std::chrono::high_resolution_clock::now();
    auto ct_avx2 = avx2_engine.encrypt_ctr(iv, pt);
    auto t4 = std::chrono::high_resolution_clock::now();
    auto ms_avx2 = std::chrono::duration_cast<std::chrono::milliseconds>(t4 - t3).count();
    std::cout << "done.\n";

    // ── Verification ──────────────────────────────────────────────
    bool match = (ct_standard == ct_avx2);

    // ── Results table ──────────────────────────────────────────────
    double throughput_std = (ms_standard > 0)
        ? (static_cast<double>(size_mb) * 1000.0 / ms_standard) : 0.0;
    double throughput_avx = (ms_avx2 > 0)
        ? (static_cast<double>(size_mb) * 1000.0 / ms_avx2) : 0.0;
    double speedup = (ms_avx2 > 0)
        ? static_cast<double>(ms_standard) / ms_avx2 : 0.0;

    std::cout << "\n";
    std::cout << "  ╔══════════════════════════════════════════════════════╗\n";
    std::cout << "  ║        " << std::setw(4) << size_mb << " MB  ENCRYPTION  RESULTS"
              << "                     ║\n";
    std::cout << "  ╠════════════════════╤═════════════╤══════════════════╣\n";
    std::cout << "  ║  Engine            │  Wall Time  │  Throughput      ║\n";
    std::cout << "  ╠════════════════════╪═════════════╪══════════════════╣\n";

    std::cout << "  ║  SM4Standard       │  " << std::setw(6) << ms_standard
              << " ms   │  " << std::fixed << std::setprecision(1)
              << std::setw(7) << throughput_std << " MB/s     ║\n";

    std::cout << "  ║  SM4AVX2           │  " << std::setw(6) << ms_avx2
              << " ms   │  " << std::fixed << std::setprecision(1)
              << std::setw(7) << throughput_avx << " MB/s     ║\n";

    std::cout << "  ╠════════════════════╧═════════════╧══════════════════╣\n";
    std::cout << "  ║  AVX2 Acceleration Ratio:  "
              << std::fixed << std::setprecision(1) << speedup << "x"
              << "                        ║\n";
    std::cout << "  ╚══════════════════════════════════════════════════════╝\n";

    // ── Correctness box ───────────────────────────────────────────
    std::cout << "\n  ";
    if (match) {
        std::cout << "[✓] SUCCESS: AVX2 ciphertext byte-for-byte identical to scalar baseline.\n";
    } else {
        std::cout << "[✗] FATAL: AVX2 output diverges from scalar baseline!\n";
    }

    _mm_free(raw_buf);
}

// ── Menu handler: Encrypt file to disk (V2: PBKDF2 + HMAC-SM3) ────────

void do_encrypt_file() {
    std::cout << "\n  Enter path to file to encrypt (empty line = cancel):\n  > " << std::flush;

    std::string path;
    if (!std::getline(std::cin, path) || path.empty()) {
        std::cout << "  [!] Cancelled.\n";
        return;
    }

    std::cout << "  Enter encryption password (empty line = cancel):\n  > " << std::flush;
    std::string password;
    if (!std::getline(std::cin, password) || password.empty()) {
        std::cout << "  [!] Cancelled.\n";
        return;
    }

    // Read source file
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs) {
        std::cout << "  [✗] Error: Cannot open file \"" << path << "\".\n";
        return;
    }

    std::streamsize fsize = ifs.tellg();
    if (fsize <= 0) {
        std::cout << "  [✗] Error: File is empty or unreadable.\n";
        return;
    }

    ifs.seekg(0, std::ios::beg);
    std::vector<uint8_t> file_data(static_cast<size_t>(fsize));
    if (!ifs.read(reinterpret_cast<char*>(file_data.data()), fsize)) {
        std::cout << "  [✗] Error: Failed to read file data.\n";
        return;
    }
    ifs.close();

    auto t_start = std::chrono::high_resolution_clock::now();

    // Generate per-file salt and IV
    auto salt = random_bytes(16);
    auto file_iv = random_bytes(16);

    // Derive SM4 and HMAC keys from password + salt
    std::vector<uint8_t> sm4_key, hmac_key;
    derive_keys(password, salt, sm4_key, hmac_key);

    // Encrypt with derived SM4 key
    SM4AVX2 file_engine(sm4_key);
    auto ciphertext = file_engine.encrypt_ctr(file_iv, file_data);

    // Build V2 header: 4 magic + 16 salt + 16 IV = 36 bytes
    std::vector<uint8_t> header(36);
    header[0] = 'S'; header[1] = 'M'; header[2] = '4'; header[3] = 'X';
    std::memcpy(header.data() + 4,  salt.data(), 16);
    std::memcpy(header.data() + 20, file_iv.data(), 16);

    // HMAC over header + ciphertext
    std::vector<uint8_t> hmac_input = header;
    hmac_input.insert(hmac_input.end(), ciphertext.begin(), ciphertext.end());
    auto hmac = calc_hmac_sm3(hmac_key, hmac_input);

    // Write: header + ciphertext + footer (HMAC)
    std::string out_path = path + ".sm4x";
    std::ofstream ofs(out_path, std::ios::binary);
    if (!ofs) {
        std::cout << "  [✗] Error: Cannot write output file \"" << out_path << "\".\n";
        return;
    }
    ofs.write(reinterpret_cast<const char*>(header.data()), 36);
    ofs.write(reinterpret_cast<const char*>(ciphertext.data()),
              static_cast<std::streamsize>(ciphertext.size()));
    ofs.write(reinterpret_cast<const char*>(hmac.data()), 32);
    ofs.close();

    auto t_end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();

    std::cout << "\n";
    std::cout << "  ╔══════════════════════════════════════════════════════╗\n";
    std::cout << "  ║        FILE  ENCRYPTED  SUCCESSFULLY  (V2)          ║\n";
    std::cout << "  ╠══════════════════════════════════════════════════════╣\n";
    std::cout << "  ║  Source       : " << std::left << std::setw(35) << path << "║\n";
    std::cout << "  ║  Output       : " << std::left << std::setw(35) << out_path << "║\n";
    std::cout << "  ║  Original     : " << std::left << std::setw(35)
              << (std::to_string(file_data.size()) + " bytes") << "║\n";
    std::cout << "  ║  Ciphertext   : " << std::left << std::setw(35)
              << (std::to_string(ciphertext.size()) + " bytes") << "║\n";
    std::cout << "  ║  Overhead     : " << std::left << std::setw(35)
              << "68 bytes (36B header + 32B HMAC)" << "║\n";
    std::cout << "  ║  KDF          : " << std::left << std::setw(35)
              << "PBKDF2-SM3 (100000 iter)" << "║\n";
    std::cout << "  ║  Integrity    : " << std::left << std::setw(35)
              << "HMAC-SM3 (encrypt-then-MAC)" << "║\n";
    std::cout << "  ║  Wall Time    : " << std::left << std::setw(35)
              << (std::to_string(ms) + " ms") << "║\n";
    std::cout << "  ╚══════════════════════════════════════════════════════╝\n";
    std::cout << "  [✓] File is self-contained: Salt + IV + HMAC all embedded.\n";
}

// ── Menu handler: Decrypt file from disk (V2: HMAC verification) ───────

void do_decrypt_file() {
    std::cout << "\n  Enter path to .sm4x file (empty line = cancel):\n  > " << std::flush;

    std::string path;
    if (!std::getline(std::cin, path) || path.empty()) {
        std::cout << "  [!] Cancelled.\n";
        return;
    }

    std::cout << "  Enter decryption password (empty line = cancel):\n  > " << std::flush;
    std::string password;
    if (!std::getline(std::cin, password) || password.empty()) {
        std::cout << "  [!] Cancelled.\n";
        return;
    }

    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs) {
        std::cout << "  [✗] Error: Cannot open file \"" << path << "\".\n";
        return;
    }

    std::streamsize fsize = ifs.tellg();
    if (fsize < 68) {
        std::cout << "  [✗] Error: File too small to be a valid V2 .sm4x container (< 68 bytes).\n";
        return;
    }

    ifs.seekg(0, std::ios::beg);

    // Read entire file into memory
    std::vector<uint8_t> file_bytes(static_cast<size_t>(fsize));
    if (!ifs.read(reinterpret_cast<char*>(file_bytes.data()), fsize)) {
        std::cout << "  [✗] Error: Failed to read file.\n";
        return;
    }
    ifs.close();

    // Validate magic bytes
    if (file_bytes[0] != 'S' || file_bytes[1] != 'M' ||
        file_bytes[2] != '4' || file_bytes[3] != 'X') {
        std::cout << "  [✗] Error: 无效的安全存储文件格式 (Magic Bytes Mismatch)\n";
        std::cout << "          Expected: SM4X  Got: "
                  << file_bytes[0] << file_bytes[1]
                  << file_bytes[2] << file_bytes[3] << "\n";
        return;
    }

    // Extract header components
    std::vector<uint8_t> salt(file_bytes.begin() + 4,  file_bytes.begin() + 20);
    std::vector<uint8_t> file_iv(file_bytes.begin() + 20, file_bytes.begin() + 36);

    // Extract ciphertext and stored HMAC
    size_t ct_size = static_cast<size_t>(fsize) - 68;
    std::vector<uint8_t> ciphertext(file_bytes.begin() + 36, file_bytes.begin() + 36 + ct_size);
    std::vector<uint8_t> stored_hmac(file_bytes.end() - 32, file_bytes.end());

    // Header + Ciphertext (everything except the footer)
    std::vector<uint8_t> integrity_region(file_bytes.begin(), file_bytes.end() - 32);

    // Derive keys from password + salt
    std::vector<uint8_t> sm4_key, hmac_key;
    derive_keys(password, salt, sm4_key, hmac_key);

    // Verify HMAC with constant-time comparison
    auto computed_hmac = calc_hmac_sm3(hmac_key, integrity_region);

    if (computed_hmac.size() != stored_hmac.size() ||
        CRYPTO_memcmp(computed_hmac.data(), stored_hmac.data(), computed_hmac.size()) != 0) {
        std::cout << "\n  ╔══════════════════════════════════════════════════════╗\n";
        std::cout << "  ║   FATAL: 完整性校验失败！                            ║\n";
        std::cout << "  ║   密码错误或文件已被篡改！                            ║\n";
        std::cout << "  ║   FATAL: Integrity check FAILED!                    ║\n";
        std::cout << "  ║   Wrong password or file has been tampered with!    ║\n";
        std::cout << "  ╚══════════════════════════════════════════════════════╝\n";
        std::cout << "  [✗] Decryption ABORTED — data integrity cannot be guaranteed.\n";
        return;
    }

    std::cout << "  [✓] HMAC-SM3 verified — file integrity confirmed.\n";

    auto t_start = std::chrono::high_resolution_clock::now();

    // Decrypt with derived SM4 key
    SM4AVX2 file_engine(sm4_key);
    auto plaintext = file_engine.encrypt_ctr(file_iv, ciphertext);

    auto t_end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();

    // Build output filename: strip .sm4x, insert _decrypted before extension
    std::string out_path = path;
    if (out_path.size() >= 5 && out_path.substr(out_path.size() - 5) == ".sm4x")
        out_path.resize(out_path.size() - 5);

    auto dot_pos = out_path.find_last_of('.');
    if (dot_pos != std::string::npos)
        out_path = out_path.substr(0, dot_pos) + "_decrypted" + out_path.substr(dot_pos);
    else
        out_path += "_decrypted";

    std::ofstream ofs(out_path, std::ios::binary);
    if (!ofs) {
        std::cout << "  [✗] Error: Cannot write output file \"" << out_path << "\".\n";
        return;
    }
    ofs.write(reinterpret_cast<const char*>(plaintext.data()),
              static_cast<std::streamsize>(plaintext.size()));
    ofs.close();

    std::cout << "\n";
    std::cout << "  ╔══════════════════════════════════════════════════════╗\n";
    std::cout << "  ║        FILE  DECRYPTED  SUCCESSFULLY  (V2)          ║\n";
    std::cout << "  ╠══════════════════════════════════════════════════════╣\n";
    std::cout << "  ║  Source       : " << std::left << std::setw(35) << path << "║\n";
    std::cout << "  ║  Output       : " << std::left << std::setw(35) << out_path << "║\n";
    std::cout << "  ║  Plaintext    : " << std::left << std::setw(35)
              << (std::to_string(plaintext.size()) + " bytes") << "║\n";
    std::cout << "  ║  Wall Time    : " << std::left << std::setw(35)
              << (std::to_string(ms) + " ms") << "║\n";
    std::cout << "  ╚══════════════════════════════════════════════════════╝\n";
    std::cout << "  [✓] Decryption complete — Encrypt-then-MAC guarantees integrity.\n";
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════
//  Entry point
// ═══════════════════════════════════════════════════════════════════════

int main() {
    banner();

    // ── Session key & IV (generated once, reused for entire session) ─
    thick_sep("SESSION PARAMETERS");

    const auto session_key = random_bytes(16);
    const auto session_iv  = random_bytes(16);

    print_hex_line("  Session Key  (16 B): ", session_key);
    print_hex_line("  Session IV   (16 B): ", session_iv);
    std::cout << "\n  These are fixed for the entire session. All encrypt/decrypt\n"
              << "  operations use the same key & IV for cross-reference testing.\n";

    SM4AVX2 engine(session_key);

    // ── Main REPL loop ──────────────────────────────────────────────
    while (true) {
        print_menu();

        std::string choice;
        if (!std::getline(std::cin, choice)) {
            std::cout << "\n  [EOF] Exiting.\n";
            break;
        }

        if (choice == "1") {
            do_encrypt(engine, session_iv);
        } else if (choice == "2") {
            do_decrypt(engine, session_iv);
        } else if (choice == "3") {
            do_benchmark(session_key, session_iv);
        } else if (choice == "4") {
            std::cout << "\n  Exiting MyCryptoEngine console. Goodbye.\n\n";
            break;
        } else if (choice == "5") {
            do_encrypt_file();
        } else if (choice == "6") {
            do_decrypt_file();
        } else if (!choice.empty()) {
            std::cout << "  [!] Unknown option '" << choice
                      << "'. Enter 1, 2, 3, 4, 5, or 6.\n";
        }
    }

    return 0;
}
