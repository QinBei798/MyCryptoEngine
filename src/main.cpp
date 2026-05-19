#include "sm4_standard.h"
#include "sm4_avx2.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <immintrin.h>

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
        } else if (!choice.empty()) {
            std::cout << "  [!] Unknown option '" << choice
                      << "'. Enter 1, 2, 3, or 4.\n";
        }
    }

    return 0;
}
