#include <benchmark/benchmark.h>

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

#include <immintrin.h>

#include "sm4_standard.h"
#include "sm4_avx2.h"

namespace {

// Fixed test key (GM/T 0002-2012 standard vector key)
const std::vector<uint8_t> kBenchKey = {
    0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
    0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
};

// Fixed IV
const std::vector<uint8_t> kBenchIV = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
};

// Generate aligned random plaintext once per benchmark registration.
// Returns _mm_malloc'd buffer; caller must _mm_free.
uint8_t* make_aligned_plaintext(size_t size) {
    uint8_t* buf = static_cast<uint8_t*>(_mm_malloc(size, 32));
    std::mt19937_64 rng(0xDEADBEEFCAFE0420ULL);
    std::uniform_int_distribution<int> dist(0, 255);
    for (size_t i = 0; i < size; ++i) {
        buf[i] = static_cast<uint8_t>(dist(rng));
    }
    return buf;
}

// ── SM4Standard benchmarks ──

void BM_SM4_Standard_10MB(benchmark::State& state) {
    constexpr size_t kSize = 10ULL * 1024 * 1024;
    SM4Standard engine(kBenchKey);
    uint8_t* plaintext = make_aligned_plaintext(kSize);

    std::vector<uint8_t> pt(plaintext, plaintext + kSize);

    for (auto _ : state) {
        auto cipher = engine.encrypt_ctr(kBenchIV, pt);
        benchmark::DoNotOptimize(cipher);
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * kSize);
    _mm_free(plaintext);
}
BENCHMARK(BM_SM4_Standard_10MB)->Unit(benchmark::kMillisecond);

void BM_SM4_Standard_100MB(benchmark::State& state) {
    constexpr size_t kSize = 100ULL * 1024 * 1024;
    SM4Standard engine(kBenchKey);
    uint8_t* plaintext = make_aligned_plaintext(kSize);
    std::vector<uint8_t> pt(plaintext, plaintext + kSize);

    for (auto _ : state) {
        auto cipher = engine.encrypt_ctr(kBenchIV, pt);
        benchmark::DoNotOptimize(cipher);
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * kSize);
    _mm_free(plaintext);
}
BENCHMARK(BM_SM4_Standard_100MB)->Unit(benchmark::kMillisecond);

// ── SM4AVX2 benchmarks ──

void BM_SM4_AVX2_10MB(benchmark::State& state) {
    constexpr size_t kSize = 10ULL * 1024 * 1024;
    SM4AVX2 engine(kBenchKey);
    uint8_t* plaintext = make_aligned_plaintext(kSize);
    std::vector<uint8_t> pt(plaintext, plaintext + kSize);

    for (auto _ : state) {
        auto cipher = engine.encrypt_ctr(kBenchIV, pt);
        benchmark::DoNotOptimize(cipher);
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * kSize);
    _mm_free(plaintext);
}
BENCHMARK(BM_SM4_AVX2_10MB)->Unit(benchmark::kMillisecond);

void BM_SM4_AVX2_100MB(benchmark::State& state) {
    constexpr size_t kSize = 100ULL * 1024 * 1024;
    SM4AVX2 engine(kBenchKey);
    uint8_t* plaintext = make_aligned_plaintext(kSize);
    std::vector<uint8_t> pt(plaintext, plaintext + kSize);

    for (auto _ : state) {
        auto cipher = engine.encrypt_ctr(kBenchIV, pt);
        benchmark::DoNotOptimize(cipher);
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * kSize);
    _mm_free(plaintext);
}
BENCHMARK(BM_SM4_AVX2_100MB)->Unit(benchmark::kMillisecond);

}  // namespace

BENCHMARK_MAIN();
