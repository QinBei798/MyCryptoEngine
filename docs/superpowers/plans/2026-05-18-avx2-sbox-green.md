# AVX2 Parallel S-Box (τ Transform) — GREEN Phase Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement `SM4AVX2::parallel_sbox_16` using `_mm256_i32gather_epi32` to perform byte-level S-box lookup on 16 32-bit words in parallel, passing the existing RED test.

**Architecture:** The τ transform applies the SM4 S-box to each of the 4 bytes of a 32-bit word independently. We use 8-way gather instructions: extract each byte position as an index vector, gather from a widened 32-bit S-box table, shift results back to their byte lanes, and OR-combine. Two `__m256i` registers (16 words total) are processed in-place.

**Tech Stack:** C++17, AVX2 intrinsics (`immintrin.h`), Google Test

---

## File Map

| Role | Path | Action |
|------|------|--------|
| Implementation | `src/sm4_avx2.cpp` | Modify: replace stub with gather-based implementation, add `SBOX_32` table |
| Test | `tests/test_sm4_avx2_sbox.cpp` | No change (already written in RED phase) |
| Build | `tests/CMakeLists.txt` | No change (already registered) |

---

### Task 1: Add `SBOX_32` widened lookup table

**Files:**
- Modify: `src/sm4_avx2.cpp` (anonymous namespace, near existing SBOX declaration)

**Why:** `_mm256_i32gather_epi32` loads 32-bit elements. The S-box is 256 `uint8_t` values. Each must be zero-extended to `uint32_t` so the gather instruction can index into a `uint32_t` array with `scale=4`.

- [ ] **Step 1: Insert `SBOX_32` after the existing `SBOX` declaration**

Locate line 42 (`};` closing the existing `SBOX` array) in `src/sm4_avx2.cpp`. Insert immediately after:

```cpp
// Widened S-box for _mm256_i32gather_epi32 (scale=4).
// SBOX_32[i] = static_cast<uint32_t>(SBOX[i])
alignas(32) constexpr uint32_t SBOX_32[256] = {
    0xd6, 0x90, 0xe9, 0xfe, 0xcc, 0xe1, 0x3d, 0xb7,
    0x16, 0xb6, 0x14, 0xc2, 0x28, 0xfb, 0x2c, 0x05,
    0x2b, 0x67, 0x9a, 0x76, 0x2a, 0xbe, 0x04, 0xc3,
    0xaa, 0x44, 0x13, 0x26, 0x49, 0x86, 0x06, 0x99,
    0x9c, 0x42, 0x50, 0xf4, 0x91, 0xef, 0x98, 0x7a,
    0x33, 0x54, 0x0b, 0x43, 0xed, 0xcf, 0xac, 0x62,
    0xe4, 0xb3, 0x1c, 0xa9, 0xc9, 0x08, 0xe8, 0x95,
    0x80, 0xdf, 0x94, 0xfa, 0x75, 0x8f, 0x3f, 0xa6,
    0x47, 0x07, 0xa7, 0xfc, 0xf3, 0x73, 0x17, 0xba,
    0x83, 0x59, 0x3c, 0x19, 0xe6, 0x85, 0x4f, 0xa8,
    0x68, 0x6b, 0x81, 0xb2, 0x71, 0x64, 0xda, 0x8b,
    0xf8, 0xeb, 0x0f, 0x4b, 0x70, 0x56, 0x9d, 0x35,
    0x1e, 0x24, 0x0e, 0x5e, 0x63, 0x58, 0xd1, 0xa2,
    0x25, 0x22, 0x7c, 0x3b, 0x01, 0x21, 0x78, 0x87,
    0xd4, 0x00, 0x46, 0x57, 0x9f, 0xd3, 0x27, 0x52,
    0x4c, 0x36, 0x02, 0xe7, 0xa0, 0xc4, 0xc8, 0x9e,
    0xea, 0xbf, 0x8a, 0xd2, 0x40, 0xc7, 0x38, 0xb5,
    0xa3, 0xf7, 0xf2, 0xce, 0xf9, 0x61, 0x15, 0xa1,
    0xe0, 0xae, 0x5d, 0xa4, 0x9b, 0x34, 0x1a, 0x55,
    0xad, 0x93, 0x32, 0x30, 0xf5, 0x8c, 0xb1, 0xe3,
    0x1d, 0xf6, 0xe2, 0x2e, 0x82, 0x66, 0xca, 0x60,
    0xc0, 0x29, 0x23, 0xab, 0x0d, 0x53, 0x4e, 0x6f,
    0xd5, 0xdb, 0x37, 0x45, 0xde, 0xfd, 0x8e, 0x2f,
    0x03, 0xff, 0x6a, 0x72, 0x6d, 0x6c, 0x5b, 0x51,
    0x8d, 0x1b, 0xaf, 0x92, 0xbb, 0xdd, 0xbc, 0x7f,
    0x11, 0xd9, 0x5c, 0x41, 0x1f, 0x10, 0x5a, 0xd8,
    0x0a, 0xc1, 0x31, 0x88, 0xa5, 0xcd, 0x7b, 0xbd,
    0x2d, 0x74, 0xd0, 0x12, 0xb8, 0xe5, 0xb4, 0xb0,
    0x89, 0x69, 0x97, 0x4a, 0x0c, 0x96, 0x77, 0x7e,
    0x65, 0xb9, 0xf1, 0x09, 0xc5, 0x6e, 0xc6, 0x84,
    0x18, 0xf0, 0x7d, 0xec, 0x3a, 0xdc, 0x4d, 0x20,
    0x79, 0xee, 0x5f, 0x3e, 0xd7, 0xcb, 0x39, 0x48,
};
```

- [ ] **Step 2: Build to verify no compile errors**

```bash
cd /mnt/d/MyCryptoEngine/build && cmake --build . --target sm4_avx2 -j$(nproc)
```

Expected: `[100%] Built target sm4_avx2`

---

### Task 2: Implement `parallel_sbox_16` using gather instructions

**Files:**
- Modify: `src/sm4_avx2.cpp` (replace the Phase C stub, ~line 217)

- [ ] **Step 1: Replace the stub body**

Locate the stub (currently around line 217):
```cpp
// ---------------------------------------------------------------------------
// Phase C Step A: AVX2 parallel S-box — RED stub
// ---------------------------------------------------------------------------

void SM4AVX2::parallel_sbox_16(__m256i* in_out_words) {
    in_out_words[0] = _mm256_setzero_si256();
    in_out_words[1] = _mm256_setzero_si256();
}
```

Replace the entire function body with the full gather-based implementation:

```cpp
void SM4AVX2::parallel_sbox_16(__m256i* in_out_words) {
    const __m256i mask_ff = _mm256_set1_epi32(0xFF);

    for (int r = 0; r < 2; ++r) {
        __m256i x = in_out_words[r];

        // Byte-slice: extract each byte position as a gather index
        __m256i idx0 = _mm256_and_si256(x, mask_ff);
        __m256i idx1 = _mm256_and_si256(_mm256_srli_epi32(x, 8), mask_ff);
        __m256i idx2 = _mm256_and_si256(_mm256_srli_epi32(x, 16), mask_ff);
        __m256i idx3 = _mm256_srli_epi32(x, 24);  // no mask needed — SBOX_32 indices are in [0,255]

        // Parallel gather from widened S-box (scale=4 for uint32_t)
        __m256i t0 = _mm256_i32gather_epi32(reinterpret_cast<const int*>(SBOX_32), idx0, 4);
        __m256i t1 = _mm256_i32gather_epi32(reinterpret_cast<const int*>(SBOX_32), idx1, 4);
        __m256i t2 = _mm256_i32gather_epi32(reinterpret_cast<const int*>(SBOX_32), idx2, 4);
        __m256i t3 = _mm256_i32gather_epi32(reinterpret_cast<const int*>(SBOX_32), idx3, 4);

        // Shift each byte back to its original lane position and combine
        // t0: byte 0 → already at bits [0..7], no shift
        // t1: byte 1 → shift left 8  to bits [8..15]
        // t2: byte 2 → shift left 16 to bits [16..23]
        // t3: byte 3 → shift left 24 to bits [24..31]
        __m256i result = _mm256_or_si256(
            t0,
            _mm256_or_si256(
                _mm256_slli_epi32(t1, 8),
                _mm256_or_si256(
                    _mm256_slli_epi32(t2, 16),
                    _mm256_slli_epi32(t3, 24))));

        in_out_words[r] = result;
    }
}
```

Also update the preceding comment block to reflect GREEN status:
```cpp
// ---------------------------------------------------------------------------
// Phase C Step A: AVX2 parallel S-box (τ transform) — GREEN
// Byte-slice → gather from SBOX_32 → shift → OR-combine
// ---------------------------------------------------------------------------
```

- [ ] **Step 2: Build the sbox test target**

```bash
cd /mnt/d/MyCryptoEngine/build && cmake --build . --target test_sm4_avx2_sbox -j$(nproc)
```

Expected: `[100%] Built target test_sm4_avx2_sbox` with zero warnings.

---

### Task 3: Run test and verify GREEN

- [ ] **Step 1: Run the sbox test**

```bash
ctest -R Sbox --output-on-failure
```

Expected:
```
[  PASSED  ] 1 test.
100% tests passed
```

**If this fails** (mismatch or segfault): invoke `superpowers:systematic-debugging` before fixing. Common pitfalls and their root causes:

| Symptom | Root Cause | Fix |
|---------|-----------|-----|
| `idx3` values > 255 crash gather | Right-shift alone doesn't mask — but all valid uint32_t bytes are in [0,255], so `(x >> 24) & 0xFF` is always safe. The implementation above drops the mask for `idx3` because `_mm256_srli_epi32(x, 24)` with valid 32-bit input produces values in [0,255] naturally. | If x contains garbage bits above bit 31 (impossible for uint32_t), add `_mm256_and_si256(_mm256_srli_epi32(x, 24), mask_ff)`. |
| Byte order wrong in output | Shift amounts incorrect | Verify: byte 0 stays in bits 0-7 (no shift), byte 1 goes to bits 8-15 (shift 8), byte 2 to bits 16-23 (shift 16), byte 3 to bits 24-31 (shift 24). |
| Gather returns wrong values | `SBOX_32` indices not matching `SBOX` | Verify `SBOX_32[i] == SBOX[i]` for all 256 entries. |

- [ ] **Step 2: Run the full test suite to check for regressions**

```bash
ctest --output-on-failure
```

Expected: All previously-passing tests remain GREEN; only the sbox test changes from RED to GREEN. The consistency test (`EncryptCTRMatchesScalarBaseline1MB`) will still FAIL because `encrypt_ctr` has not yet been wired to use actual encryption — this is expected.

---

### Task 4: Commit

- [ ] **Step 1: Commit the changes**

```bash
git add src/sm4_avx2.cpp
git commit -m "feat: implement AVX2 parallel S-box (τ transform) via gather

Replace the RED stub in parallel_sbox_16 with byte-slice + gather
from widened SBOX_32 + shift + OR-combine. Processes 16 32-bit words
(2 × __m256i) in-place. All sbox tests pass against scalar ground truth."
```

---

## Self-Review Checklist

1. **Spec coverage:**
   - [x] `SBOX_32` widened table (Task 1)
   - [x] Byte-slicing with `_mm256_and_si256` / `_mm256_srli_epi32` (Task 2)
   - [x] `_mm256_i32gather_epi32` with scale=4 (Task 2)
   - [x] Shift + OR reconstruction (Task 2)
   - [x] Loop over 2 `__m256i` registers (Task 2)
   - [x] Build verification (Tasks 1, 2)
   - [x] Test run + GREEN verification (Task 3)
   - [x] Regression check (Task 3)
   - [x] Commit (Task 4)

2. **Placeholder scan:** Zero placeholders — all code, commands, and expected output are explicit.

3. **Type consistency:** `SBOX_32` is `const uint32_t[256]` in anonymous namespace; `reinterpret_cast<const int*>(SBOX_32)` is correct for the gather intrinsic signature.
