# Google Benchmark Integration — SM4-CTR Throughput Measurement

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Integrate Google Benchmark, measure `SM4Standard::encrypt_ctr` and `SM4AVX2::encrypt_ctr` throughput at 10MB/100MB data sizes, and produce MB/s comparison data.

**Architecture:** `FetchContent` pulls `google/benchmark`. A new `benchmarks/` directory holds a single `sm4_benchmark.cpp` with four benchmark cases. All buffers use `_mm_malloc` (32-byte aligned) to match AVX2 requirements. The benchmark loop contains only the `encrypt_ctr` call; all setup (key, IV, plaintext generation) is outside the loop. `state.SetBytesProcessed()` enables automatic MB/s reporting.

**Tech Stack:** C++17, Google Benchmark, AVX2, CMake FetchContent

**Red line:** Absolutely NO modifications to `src/` or `include/` — existing crypto code is frozen.

---

## File Map

| Role | Path | Action |
|------|------|--------|
| Build (root) | `CMakeLists.txt` | Modify: add benchmark FetchContent and subdirectory |
| Build (bench) | `benchmarks/CMakeLists.txt` | Create |
| Benchmark | `benchmarks/sm4_benchmark.cpp` | Create |

---

### Task 1: Integrate Google Benchmark into the build system

**Files:**
- Modify: `CMakeLists.txt` (root, `/mnt/d/MyCryptoEngine/CMakeLists.txt`)
- Create: `benchmarks/CMakeLists.txt`

**Why:** Google Benchmark must be fetched via CMake so `find_package(benchmark)` succeeds. The existing project already uses `FetchContent` for Google Test — the same pattern applies.

- [ ] **Step 1: Add benchmark FetchContent and subdirectory to root CMakeLists.txt**

Add after the existing `FetchContent_MakeAvailable(googletest)` block (after line 16):

```cmake
FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.9.1
)
FetchContent_MakeAvailable(googlebenchmark)
```

Add `add_subdirectory(benchmarks)` after `add_subdirectory(tests)` (after line 29).

The final root `CMakeLists.txt` will be:

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyCryptoEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -mavx2 -Wall -Wextra -Wpedantic")

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
)
FetchContent_MakeAvailable(googletest)

FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.9.1
)
FetchContent_MakeAvailable(googlebenchmark)

include_directories(${CMAKE_SOURCE_DIR}/include)

add_library(sm4_core STATIC
    src/sm4_standard.cpp
)

add_library(sm4_avx2 STATIC
    src/sm4_avx2.cpp
)

enable_testing()
add_subdirectory(tests)
add_subdirectory(benchmarks)
```

- [ ] **Step 2: Create `benchmarks/CMakeLists.txt`**

Create the file `/mnt/d/MyCryptoEngine/benchmarks/CMakeLists.txt`:

```cmake
add_executable(sm4_benchmark
    sm4_benchmark.cpp
)

target_link_libraries(sm4_benchmark
    sm4_avx2
    sm4_core
    benchmark::benchmark
)
```

- [ ] **Step 3: Create the `benchmarks/` directory and verify CMake configure**

```bash
mkdir -p /mnt/d/MyCryptoEngine/benchmarks
cd /mnt/d/MyCryptoEngine/build && cmake .. 2>&1
```

Expected: CMake configures without errors, Google Benchmark is fetched and built.

---

### Task 2: Write the benchmark source

**Files:**
- Create: `benchmarks/sm4_benchmark.cpp`

**Why:** The benchmark measures raw `encrypt_ctr` throughput. All buffers are `_mm_malloc`-allocated with 32-byte alignment. Random plaintext is generated once per benchmark (outside the `for (auto _ : state)` loop). `state.SetBytesProcessed()` reports MB/s.

- [ ] **Step 1: Write `benchmarks/sm4_benchmark.cpp`**

```cpp
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

    for (auto _ : state) {
        auto cipher = engine.encrypt_ctr(kBenchIV, std::vector<uint8_t>(plaintext, plaintext + kSize));
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

    for (auto _ : state) {
        auto cipher = engine.encrypt_ctr(kBenchIV, std::vector<uint8_t>(plaintext, plaintext + kSize));
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

    for (auto _ : state) {
        auto cipher = engine.encrypt_ctr(kBenchIV, std::vector<uint8_t>(plaintext, plaintext + kSize));
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

    for (auto _ : state) {
        auto cipher = engine.encrypt_ctr(kBenchIV, std::vector<uint8_t>(plaintext, plaintext + kSize));
        benchmark::DoNotOptimize(cipher);
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * kSize);
    _mm_free(plaintext);
}
BENCHMARK(BM_SM4_AVX2_100MB)->Unit(benchmark::kMillisecond);

}  // namespace

BENCHMARK_MAIN();
```

- [ ] **Step 2: Verify compilation**

```bash
cd /mnt/d/MyCryptoEngine/build && cmake --build . --target sm4_benchmark -j$(nproc) 2>&1
```

Expected: `[100%] Built target sm4_benchmark` with zero warnings.

---

### Task 3: Run benchmarks and verify output

- [ ] **Step 1: Run the benchmark binary**

```bash
cd /mnt/d/MyCryptoEngine/build && ./benchmarks/sm4_benchmark 2>&1
```

Expected: Google Benchmark output table showing all four benchmarks with MB/s throughput data.

- [ ] **Step 2: Run full test suite to confirm zero regressions**

```bash
cd /mnt/d/MyCryptoEngine/build && ctest --output-on-failure 2>&1
```

Expected: `100% tests passed, 0 tests failed out of 14`.

---

### Task 4: Commit

- [ ] **Step 1: Commit all changes**

```bash
git add CMakeLists.txt benchmarks/ docs/superpowers/plans/2026-05-18-benchmark-integration.md
git commit -m "feat: integrate Google Benchmark for SM4-CTR throughput measurement

Add 10MB/100MB benchmarks for SM4Standard and SM4AVX2 encrypt_ctr
with 32-byte aligned buffers. FetchContent pulls google/benchmark v1.9.1."
```

---

## Self-Review Checklist

1. **Spec coverage:**
   - [x] Google Benchmark via FetchContent (Task 1)
   - [x] `sm4_benchmark` executable target + CMakeLists (Task 1)
   - [x] 4 benchmark cases: Standard 10MB, Standard 100MB, AVX2 10MB, AVX2 100MB (Task 2)
   - [x] All buffers 32-byte aligned (`_mm_malloc`) (Task 2)
   - [x] `SetBytesProcessed()` for MB/s reporting (Task 2)
   - [x] Setup outside benchmark loop, only `encrypt_ctr` inside (Task 2)
   - [x] Build verification (Task 2)
   - [x] Benchmark run (Task 3)
   - [x] Regression test suite (Task 3)
   - [x] Commit (Task 4)

2. **Placeholder scan:** Zero placeholders — all code, commands, and expected output are explicit.

3. **Type consistency:** `SM4Standard::encrypt_ctr` and `SM4AVX2::encrypt_ctr` both take `(const std::vector<uint8_t>& iv, const std::vector<uint8_t>& plaintext)` — benchmark calls match these signatures exactly.
