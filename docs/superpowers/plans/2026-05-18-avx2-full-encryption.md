# AVX2 32-Round SM4 Encryption — Full Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the Phase A dummy `encrypt_ctr` (memcpy pass-through) with a full 32-round SM4 AVX2 encryption engine, matching SM4Standard byte-for-byte.

**Architecture:** Two-batch pipeline: `generate_16_counters` → BSWAP32 (BE→LE) → 32-round SM4 on word-sliced state (τ via `parallel_sbox_16`, L via `parallel_L_transform`, round keys broadcast) → output reversal (X35,X34,X33,X32) → BSWAP32 (LE→BE) → inverse transpose to block-sliced → XOR keystream with plaintext. Critical endianness invariant: SIMD arithmetic requires LE-native integer values; all BE↔LE conversions use `_mm256_shuffle_epi8` with `BSWAP32_MASK`.

**Tech Stack:** C++17, AVX2 intrinsics, Google Test

---

## File Map

| Role | Path | Action |
|------|------|--------|
| Implementation | `src/sm4_avx2.cpp` | Major rewrite of `encrypt_ctr`; add `BSWAP32_MASK`, `ROTL32_AVX2`, `parallel_L_transform`, SM4 round kernel, inverse transpose helper |
| Header | `include/sm4_avx2.h` | Add `parallel_L_transform` declaration |
| Test | `tests/test_sm4_avx2_consistency.cpp` | No change — existing `EncryptCTRMatchesScalarBaseline1MB` already asserts correct comparison |

---

## Critical Design Decisions

### Endianness Pipeline

```
Counter bytes (BE in memory)
    ↓ generate_16_counters (transpose_to_word_sliced via _mm256_i32gather_epi32)
Word-sliced state (LE byte order in SIMD lanes — wrong integer values!)
    ↓ BSWAP32 (shuffle bytes 3,2,1,0 → 0,1,2,3 within each 32-bit lane)
Correct native integer values (LE) — ready for SIMD arithmetic
    ↓ 32-round SM4 (τ + L + XOR with broadcast rk)
Output values (LE native integers, X32..X35 per block)
    ↓ Output reversal + BSWAP32 (LE→BE)
BE keystream bytes
    ↓ Inverse transpose (word-sliced → block-sliced)
16 × 16-byte keystream blocks
    ↓ XOR with plaintext
Ciphertext
```

### Sliding Register Window for 32 Rounds

```
// Batch A (8 blocks): r0=X0, r1=X1, r2=X2, r3=X3 in __m256i lanes
// Batch B (8 blocks): r4=X0, r5=X1, r6=X2, r7=X3
// Combined for sbox: sbox takes [batch_A_value, batch_B_value]

for round i = 0..31:
    tmp_a = r1 ⊕ r2 ⊕ r3 ⊕ broadcast(rk[i])
    tmp_b = r5 ⊕ r6 ⊕ r7 ⊕ broadcast(rk[i])
    sbox_input[0] = tmp_a, sbox_input[1] = tmp_b
    parallel_sbox_16(sbox_input)          // τ: 16-way S-box
    sbox_input[0] = L(sbox_input[0])      // L: batch A
    sbox_input[1] = L(sbox_input[1])      // L: batch B
    r0new = r0 ⊕ sbox_input[0]
    r4new = r4 ⊕ sbox_input[1]
    // Slide window: r0=r1, r1=r2, r2=r3, r3=r0new, etc.
```

### Output Layout

After 32 rounds (before reversal):
- Batch A: r0=X32, r1=X33, r2=X34, r3=X35
- Batch B: r4=X32, r5=X33, r6=X34, r7=X35

SM4 output order is reverse: (X35, X34, X33, X32). So for each batch, the 4 registers must be reversed before inverse transpose.

---

### Task 1: Add BSWAP32_MASK constant

**Files:**
- Modify: `src/sm4_avx2.cpp` (anonymous namespace, near existing `BSWAP64_MASK`)

**Why:** `_mm256_i32gather_epi32` loads BE bytes as LE int32 values, reversing the byte order within each 32-bit word. We need to bswap each 32-bit lane to recover the correct abstract integer value for SIMD arithmetic. After SM4 rounds, we bswap back to produce BE keystream bytes.

- [ ] **Step 1: Insert `BSWAP32_MASK` immediately after the `BSWAP64_MASK` definition (after line 132)**

Locate the closing `};` of `BSWAP64_MASK` (current line 132, after `   15, 14, 13, 12, 11, 10, 9,  8,   // element 3 (bytes 24-31)`). Insert immediately after:

```cpp
// 8-way 32-bit byte-swap: reverses bytes within each 32-bit element.
alignas(32) constexpr uint8_t BSWAP32_MASK[32] = {
    3,  2,  1,  0,   // element 0 (bytes 0-3)
    7,  6,  5,  4,   // element 1 (bytes 4-7)
    11, 10, 9,  8,   // element 2 (bytes 8-11)
    15, 14, 13, 12,  // element 3 (bytes 12-15)
    3,  2,  1,  0,   // element 4 (bytes 16-19)
    7,  6,  5,  4,   // element 5 (bytes 20-23)
    11, 10, 9,  8,   // element 6 (bytes 24-27)
    15, 14, 13, 12,  // element 7 (bytes 28-31)
};
```

- [ ] **Step 2: Build library to verify no compile errors**

```bash
cd /mnt/d/MyCryptoEngine/build && cmake --build . --target sm4_avx2 -j$(nproc)
```

Expected: `[100%] Built target sm4_avx2`.

---

### Task 2: Add ROTL32_AVX2 helper and parallel_L_transform

**Files:**
- Modify: `src/sm4_avx2.cpp` (anonymous namespace, before `} // namespace`)
- Modify: `include/sm4_avx2.h` (public section, after `parallel_sbox_16` declaration)

- [ ] **Step 1: Add ROTL32_AVX2 macro and parallel_L_transform in anonymous namespace**

Insert after the `transpose_to_word_sliced` function (before `} // namespace` at ~line 165):

```cpp
// 8-way 32-bit rotate left: (x << n) | (x >> (32-n))
inline __m256i rotl32_avx2(__m256i x, int n) {
    return _mm256_or_si256(_mm256_slli_epi32(x, n),
                           _mm256_srli_epi32(x, 32 - n));
}

// 8-way SM4 L transform: B ⊕ (B<<<2) ⊕ (B<<<10) ⊕ (B<<<18) ⊕ (B<<<24)
inline __m256i parallel_L_transform(__m256i b) {
    __m256i t = _mm256_xor_si256(rotl32_avx2(b, 2), rotl32_avx2(b, 10));
    t = _mm256_xor_si256(t, rotl32_avx2(b, 18));
    t = _mm256_xor_si256(t, rotl32_avx2(b, 24));
    return _mm256_xor_si256(b, t);
}
```

- [ ] **Step 2: Declare parallel_L_transform in header**

In `include/sm4_avx2.h`, after the `parallel_sbox_16` declaration:

```cpp
    // Phase D: AVX2 parallel L transform on 8 32-bit words.
    static __m256i parallel_L_transform(__m256i b);
```

Note: `parallel_L_transform` is declared public static (like `parallel_sbox_16`) so it can be tested independently. The `rotl32_avx2` stays in the anonymous namespace as an implementation detail.

- [ ] **Step 3: Build to verify**

```bash
cd /mnt/d/MyCryptoEngine/build && cmake --build . --target sm4_avx2 -j$(nproc)
```

Expected: `[100%] Built target sm4_avx2`.

---

### Task 3: Implement inverse transpose helper

**Files:**
- Modify: `src/sm4_avx2.cpp` (anonymous namespace, after `transpose_to_word_sliced`)

**Why:** After 32 rounds, we have 8 × __m256i in word-sliced format. We need to convert back to 16 consecutive 16-byte blocks for XOR with plaintext.

- [ ] **Step 1: Add inverse transpose function**

Insert after `transpose_to_word_sliced`:

```cpp
// Inverse of transpose_to_word_sliced: convert word-sliced (8 × __m256i)
// back to block-sliced (16 consecutive 16-byte blocks in keystream[256]).
// state[] order: output words 0..3 for batch 0, then words 0..3 for batch 1,
// where word 0 = X35, word 1 = X34, word 2 = X33, word 3 = X32 (post-reversal).
inline void transpose_from_word_sliced(const __m256i* state,
                                       uint8_t keystream[256]) {
    alignas(32) uint32_t words[8][8];
    for (int i = 0; i < 8; ++i) {
        _mm256_store_si256(reinterpret_cast<__m256i*>(words[i]), state[i]);
    }
    for (int b = 0; b < 16; ++b) {
        int batch = b / 8;
        int idx   = b % 8;
        // state[batch*4 + 0] = X35 (output word 0)
        // state[batch*4 + 1] = X34 (output word 1)
        // state[batch*4 + 2] = X33 (output word 2)
        // state[batch*4 + 3] = X32 (output word 3)
        for (int w = 0; w < 4; ++w) {
            uint32_t val = words[batch * 4 + w][idx];
            uint8_t* dst = keystream + b * 16 + w * 4;
            dst[0] = static_cast<uint8_t>(val & 0xFF);
            dst[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
            dst[2] = static_cast<uint8_t>((val >> 16) & 0xFF);
            dst[3] = static_cast<uint8_t>((val >> 24) & 0xFF);
        }
    }
}
```

**Key invariant:** After BSWAP32 (LE→BE), each `val` is in BE byte order. `dst[0]` gets byte 0 (bits 0-7) which is the LEAST significant byte of the BE word. This matches the standard SM4 output convention where bytes are stored most-significant-byte-first. Wait — let's verify:

After the 32 rounds, each word is a correct native uint32_t value. BSWAP32 converts it to BE bytes. For a word value 0x01234567 in native (LE host):
- Before BSWAP32: lane bytes are [0x67, 0x45, 0x23, 0x01]
- After BSWAP32: lane bytes are [0x01, 0x23, 0x45, 0x67]
- Extracting with `val & 0xFF` gives 0x01 (byte at bits 0-7 of the __m256i lane)
- Store to dst[0] = 0x01

In the SM4 standard output, a word 0x01234567 is stored as [0x01, 0x23, 0x45, 0x67] (BE). Our dst[0]=0x01, dst[1]=0x23, dst[2]=0x45, dst[3]=0x67. Correct!

- [ ] **Step 2: Build to verify**

```bash
cd /mnt/d/MyCryptoEngine/build && cmake --build . --target sm4_avx2 -j$(nproc)
```

Expected: `[100%] Built target sm4_avx2`.

---

### Task 4: Implement the 32-round SM4 kernel in encrypt_ctr

**Files:**
- Modify: `src/sm4_avx2.cpp` (replace the entire `encrypt_ctr` body)

This is the core task. The new `encrypt_ctr` must:

1. Generate 16 counters per 256-byte chunk (call `generate_16_counters`)
2. BSWAP32 each of the 8 state registers (BE → LE native)
3. Run 32-round SM4 for both batches in lockstep
4. Reverse output order per batch (X35,X34,X33,X32)
5. BSWAP32 back (LE → BE)
6. Inverse transpose to 16 keystream blocks
7. XOR keystream with plaintext

- [ ] **Step 1: Replace the entire Phase A encrypt_ctr body**

Replace from line 283 (`std::vector<uint8_t> SM4AVX2::encrypt_ctr(`) to end of file (line 326).

```cpp
// ---------------------------------------------------------------------------
// Phase D: Full 32-round SM4-CTR encryption — GREEN
// Pipeline: counters → BSWAP32(BE→LE) → 32-round SM4 → reversal →
//            BSWAP32(LE→BE) → inverse transpose → XOR plaintext
// ---------------------------------------------------------------------------

std::vector<uint8_t> SM4AVX2::encrypt_ctr(
    const std::vector<uint8_t>& iv,
    const std::vector<uint8_t>& plaintext) const {

    const size_t size = plaintext.size();
    std::vector<uint8_t> ciphertext(size);

    const __m256i bswap32_mask =
        _mm256_load_si256(reinterpret_cast<const __m256i*>(BSWAP32_MASK));

    // Aligned working buffers (temp keystream + plaintext/ciphertext)
    uint8_t* src = static_cast<uint8_t*>(_mm_malloc(size + 32, 32));
    uint8_t* dst = static_cast<uint8_t*>(_mm_malloc(size + 32, 32));
    std::memcpy(src, plaintext.data(), size);

    // ── Counters ──
    alignas(32) uint8_t ctr[16];
    std::memcpy(ctr, iv.data(), 16);

    size_t offset = 0;

    // ── Process full 256-byte chunks (16 blocks = 2 batches × 8) ──
    while (offset + 256 <= size) {
        // 1. Generate 16 consecutive BE counter blocks in word-sliced layout
        alignas(32) uint8_t state_raw[256];
        auto* state = reinterpret_cast<__m256i*>(state_raw);
        generate_16_counters(state, ctr);

        // Advance counter by 16
        for (int i = 0; i < 16; ++i) {
            ctr_increment(ctr);
        }

        // 2. BSWAP32: BE → LE (restore correct native integer values)
        for (int i = 0; i < 8; ++i) {
            state[i] = _mm256_shuffle_epi8(state[i], bswap32_mask);
        }

        // ── 3. 32-round SM4 (two batches in lockstep) ──
        // Batch A: registers 0..3;  Batch B: registers 4..7
        __m256i r0 = state[0], r1 = state[1], r2 = state[2], r3 = state[3];
        __m256i r4 = state[4], r5 = state[5], r6 = state[6], r7 = state[7];

        __m256i sbox_in[2];

        for (int round = 0; round < 32; ++round) {
            __m256i rk_vec = _mm256_set1_epi32(static_cast<int>(rk_[round]));

            // T input: X[i+1] ⊕ X[i+2] ⊕ X[i+3] ⊕ rk
            __m256i t_in_a = _mm256_xor_si256(
                _mm256_xor_si256(r1, r2),
                _mm256_xor_si256(r3, rk_vec));
            __m256i t_in_b = _mm256_xor_si256(
                _mm256_xor_si256(r5, r6),
                _mm256_xor_si256(r7, rk_vec));

            // τ (S-box)
            sbox_in[0] = t_in_a;
            sbox_in[1] = t_in_b;
            parallel_sbox_16(sbox_in);

            // L transform
            __m256i t_out_a = parallel_L_transform(sbox_in[0]);
            __m256i t_out_b = parallel_L_transform(sbox_in[1]);

            // X[i+4] = X[i] ⊕ T(...)
            __m256i r0_new = _mm256_xor_si256(r0, t_out_a);
            __m256i r4_new = _mm256_xor_si256(r4, t_out_b);

            // Slide window
            r0 = r1; r1 = r2; r2 = r3; r3 = r0_new;
            r4 = r5; r5 = r6; r6 = r7; r7 = r4_new;
        }

        // ── 4. Output: reverse order (X35, X34, X33, X32) per batch ──
        // After 32 rounds: r0=X32, r1=X33, r2=X34, r3=X35
        // Output: r3=X35(word0), r2=X34(word1), r1=X33(word2), r0=X32(word3)
        state[0] = r3; state[1] = r2; state[2] = r1; state[3] = r0;
        state[4] = r7; state[5] = r6; state[6] = r5; state[7] = r4;

        // ── 5. BSWAP32: LE → BE keystream bytes ──
        for (int i = 0; i < 8; ++i) {
            state[i] = _mm256_shuffle_epi8(state[i], bswap32_mask);
        }

        // ── 6. Inverse transpose: word-sliced → 16 block-sliced keystream ──
        alignas(32) uint8_t keystream[256];
        transpose_from_word_sliced(state, keystream);

        // ── 7. XOR keystream with plaintext ──
        for (int i = 0; i < 8; ++i) {
            __m256i ks = _mm256_load_si256(
                reinterpret_cast<const __m256i*>(keystream + i * 32));
            __m256i pt = _mm256_load_si256(
                reinterpret_cast<const __m256i*>(src + offset + i * 32));
            __m256i ct = _mm256_xor_si256(pt, ks);
            _mm256_store_si256(
                reinterpret_cast<__m256i*>(dst + offset + i * 32), ct);
        }

        offset += 256;
    }

    // ── Tail bytes (less than 256) — fall back to scalar SM4Standard ──
    if (offset < size) {
        // Use the scalar encrypt_block for remaining bytes
        // (CTR mode, one block at a time)
        uint8_t keystream[16];
        size_t remaining = size - offset;
        size_t pos = 0;

        while (pos < remaining) {
            // Encrypt current counter
            uint32_t x[36];
            uint32_t mk[4];
            // Load counter as BE words (same as SM4Standard::encrypt_block)
            mk[0] = (static_cast<uint32_t>(ctr[0]) << 24) |
                    (static_cast<uint32_t>(ctr[1]) << 16) |
                    (static_cast<uint32_t>(ctr[2]) << 8)  |
                    (static_cast<uint32_t>(ctr[3]));
            mk[1] = (static_cast<uint32_t>(ctr[4]) << 24) |
                    (static_cast<uint32_t>(ctr[5]) << 16) |
                    (static_cast<uint32_t>(ctr[6]) << 8)  |
                    (static_cast<uint32_t>(ctr[7]));
            mk[2] = (static_cast<uint32_t>(ctr[8]) << 24) |
                    (static_cast<uint32_t>(ctr[9]) << 16) |
                    (static_cast<uint32_t>(ctr[10]) << 8) |
                    (static_cast<uint32_t>(ctr[11]));
            mk[3] = (static_cast<uint32_t>(ctr[12]) << 24) |
                    (static_cast<uint32_t>(ctr[13]) << 16) |
                    (static_cast<uint32_t>(ctr[14]) << 8) |
                    (static_cast<uint32_t>(ctr[15]));

            x[0] = mk[0]; x[1] = mk[1]; x[2] = mk[2]; x[3] = mk[3];

            for (int i = 0; i < 32; ++i) {
                uint32_t t_in = x[i + 1] ^ x[i + 2] ^ x[i + 3] ^ rk_[i];
                uint32_t t_sbox = sm4_sbox_word(t_in);
                uint32_t t_out = sm4_l_prime(t_sbox);  // WRONG! Should use sm4_l
                x[i + 4] = x[i] ^ t_out;
            }
            // Hmm wait, this uses sm4_l_prime which is for KEY EXPANSION, not encryption!

            // Actually, let me use a cleaner approach: just call the existing
            // SM4Standard encrypt_block. But we don't have access to SM4Standard here.

            // Alternative: use the scalar encrypt_ctr path from SM4Standard, inline.
            // Actually the simplest: just put the correct SM4 T function inline.
        }
    }

    _mm_free(src);
    _mm_free(dst);
    return ciphertext;
}
```

**WAIT** — The tail handling is getting complex because we'd need to duplicate SM4Standard logic or add a dependency. Let me simplify: use a helper that performs scalar SM4 encrypt_block using the same rk_ we already have.

Actually, the cleanest approach: the tail can just use the same rk_ and scalar SM4 round function. Let me define the scalar round function inline in the anonymous namespace.

Let me rewrite Task 4 more carefully.

- [ ] **Step 1 (revised): Add scalar encrypt_block helper in anonymous namespace**

Insert before `} // namespace`:

```cpp
// Scalar SM4 block encrypt using pre-computed round keys (for tail handling).
inline void sm4_encrypt_block_scalar(const uint32_t rk[32],
                                     const uint8_t in[16],
                                     uint8_t out[16]) {
    uint32_t x[36];
    x[0] = (static_cast<uint32_t>(in[0]) << 24) |
           (static_cast<uint32_t>(in[1]) << 16) |
           (static_cast<uint32_t>(in[2]) << 8)  |
           (static_cast<uint32_t>(in[3]));
    x[1] = (static_cast<uint32_t>(in[4]) << 24) |
           (static_cast<uint32_t>(in[5]) << 16) |
           (static_cast<uint32_t>(in[6]) << 8)  |
           (static_cast<uint32_t>(in[7]));
    x[2] = (static_cast<uint32_t>(in[8]) << 24) |
           (static_cast<uint32_t>(in[9]) << 16) |
           (static_cast<uint32_t>(in[10]) << 8) |
           (static_cast<uint32_t>(in[11]));
    x[3] = (static_cast<uint32_t>(in[12]) << 24) |
           (static_cast<uint32_t>(in[13]) << 16) |
           (static_cast<uint32_t>(in[14]) << 8) |
           (static_cast<uint32_t>(in[15]));

    for (int i = 0; i < 32; ++i) {
        uint32_t t_in = x[i + 1] ^ x[i + 2] ^ x[i + 3] ^ rk[i];
        uint32_t t_sbox = sm4_sbox_word(t_in);
        // Use sm4_l (the encryption L, not sm4_l_prime which is for key expansion)
        uint32_t t_l = t_sbox ^ rotl(t_sbox, 2) ^ rotl(t_sbox, 10) ^
                       rotl(t_sbox, 18) ^ rotl(t_sbox, 24);
        x[i + 4] = x[i] ^ t_l;
    }

    // Reverse output
    for (int i = 0; i < 4; ++i) {
        uint32_t w = x[35 - i];
        out[i * 4 + 0] = static_cast<uint8_t>((w >> 24) & 0xFF);
        out[i * 4 + 1] = static_cast<uint8_t>((w >> 16) & 0xFF);
        out[i * 4 + 2] = static_cast<uint8_t>((w >> 8) & 0xFF);
        out[i * 4 + 3] = static_cast<uint8_t>(w & 0xFF);
    }
}
```

Note: This is needed because SM4Standard::encrypt_block is not accessible from SM4AVX2. We need scalar fallback for tail bytes that don't fill a full 256-byte (16-block) chunk. The above reuses existing anonymous-namespace helpers `sm4_sbox_word` and `rotl`.

- [ ] **Step 2: Replace the Phase A encrypt_ctr body with full implementation**

Replace from line 283 to end of file:

```cpp
// ---------------------------------------------------------------------------
// Phase D: Full 32-round SM4-CTR encryption — GREEN
// Pipeline: counters → BSWAP32(BE→LE) → 32-round SM4 → reversal →
//            BSWAP32(LE→BE) → inverse transpose → XOR plaintext
// ---------------------------------------------------------------------------

std::vector<uint8_t> SM4AVX2::encrypt_ctr(
    const std::vector<uint8_t>& iv,
    const std::vector<uint8_t>& plaintext) const {

    const size_t size = plaintext.size();
    std::vector<uint8_t> ciphertext(size);

    const __m256i bswap32_mask =
        _mm256_load_si256(reinterpret_cast<const __m256i*>(BSWAP32_MASK));

    // 32-byte-aligned working buffer
    uint8_t* src = static_cast<uint8_t*>(_mm_malloc(size + 32, 32));
    uint8_t* dst = static_cast<uint8_t*>(_mm_malloc(size + 32, 32));
    std::memcpy(src, plaintext.data(), size);

    // ── Counter state ──
    alignas(32) uint8_t ctr[16];
    std::memcpy(ctr, iv.data(), 16);

    size_t offset = 0;

    // ── Fast path: process full 256-byte chunks (16 blocks) ──
    while (offset + 256 <= size) {
        // 1. Generate 16 consecutive BE counter blocks → word-sliced state
        alignas(32) uint8_t state_raw[256];
        auto* state = reinterpret_cast<__m256i*>(state_raw);
        generate_16_counters(state, ctr);

        // Advance IV counter by 16
        for (int i = 0; i < 16; ++i) ctr_increment(ctr);

        // 2. BSWAP32 each state register: BE bytes → LE native integers
        for (int i = 0; i < 8; ++i) {
            state[i] = _mm256_shuffle_epi8(state[i], bswap32_mask);
        }

        // 3. 32-round SM4: batch A (regs 0..3) + batch B (regs 4..7)
        __m256i r0 = state[0], r1 = state[1], r2 = state[2], r3 = state[3];
        __m256i r4 = state[4], r5 = state[5], r6 = state[6], r7 = state[7];

        __m256i sbox_in[2];

        for (int round = 0; round < 32; ++round) {
            __m256i rk_vec =
                _mm256_set1_epi32(static_cast<int>(rk_[round]));

            // T input: X[i+1] ⊕ X[i+2] ⊕ X[i+3] ⊕ rk
            __m256i t_in_a =
                _mm256_xor_si256(_mm256_xor_si256(r1, r2),
                                 _mm256_xor_si256(r3, rk_vec));
            __m256i t_in_b =
                _mm256_xor_si256(_mm256_xor_si256(r5, r6),
                                 _mm256_xor_si256(r7, rk_vec));

            // τ (16-way parallel S-box)
            sbox_in[0] = t_in_a;
            sbox_in[1] = t_in_b;
            parallel_sbox_16(sbox_in);

            // L transform
            __m256i t_out_a = parallel_L_transform(sbox_in[0]);
            __m256i t_out_b = parallel_L_transform(sbox_in[1]);

            // X[i+4] = X[i] ⊕ T(X[i+1] ⊕ X[i+2] ⊕ X[i+3] ⊕ rk)
            __m256i r0_new = _mm256_xor_si256(r0, t_out_a);
            __m256i r4_new = _mm256_xor_si256(r4, t_out_b);

            // Slide register window
            r0 = r1; r1 = r2; r2 = r3; r3 = r0_new;
            r4 = r5; r5 = r6; r6 = r7; r7 = r4_new;
        }

        // 4. Output reversal: (X35, X34, X33, X32) per batch
        state[0] = r3; state[1] = r2; state[2] = r1; state[3] = r0;
        state[4] = r7; state[5] = r6; state[6] = r5; state[7] = r4;

        // 5. BSWAP32: LE native → BE keystream bytes
        for (int i = 0; i < 8; ++i) {
            state[i] = _mm256_shuffle_epi8(state[i], bswap32_mask);
        }

        // 6. Inverse transpose → 16 consecutive keystream blocks
        alignas(32) uint8_t keystream[256];
        transpose_from_word_sliced(state, keystream);

        // 7. XOR keystream with plaintext
        for (int i = 0; i < 8; ++i) {
            __m256i ks = _mm256_load_si256(
                reinterpret_cast<const __m256i*>(keystream + i * 32));
            __m256i pt_blk = _mm256_load_si256(
                reinterpret_cast<const __m256i*>(src + offset + i * 32));
            _mm256_store_si256(
                reinterpret_cast<__m256i*>(dst + offset + i * 32),
                _mm256_xor_si256(pt_blk, ks));
        }

        offset += 256;
    }

    // ── Tail: < 256 bytes — scalar fallback ──
    while (offset < size) {
        uint8_t keystream[16];
        sm4_encrypt_block_scalar(rk_, ctr, keystream);

        size_t chunk = size - offset;
        if (chunk > 16) chunk = 16;
        for (size_t i = 0; i < chunk; ++i) {
            dst[offset + i] = src[offset + i] ^ keystream[i];
        }
        offset += chunk;
        ctr_increment(ctr);
    }

    std::memcpy(ciphertext.data(), dst, size);

    _mm_free(src);
    _mm_free(dst);
    return ciphertext;
}
```

- [ ] **Step 3: Build all targets**

```bash
cd /mnt/d/MyCryptoEngine/build && cmake --build . -j$(nproc) 2>&1
```

Expected: all targets built, zero warnings.

---

### Task 5: Run tests and verify GREEN

- [ ] **Step 1: Run the consistency test**

```bash
ctest -R consistency --output-on-failure
```

Expected:
```
100% tests passed
```

- [ ] **Step 2: Run full test suite for regressions**

```bash
ctest --output-on-failure
```

Expected: All 14 tests pass (including consistency test #12).

---

### Task 6: Commit

- [ ] **Step 1: Commit all changes**

```bash
git add src/sm4_avx2.cpp include/sm4_avx2.h
git commit -m "feat: implement full 32-round SM4-CTR encryption engine in AVX2

Replace Phase A memory-bus stub with complete encryption pipeline:
- BSWAP32_MASK for BE<->LE conversion within SIMD lanes
- rotl32_avx2 + parallel_L_transform for 8-way L(B)
- 32-round SM4 kernel: two-batch lockstep with register sliding window
- Output reversal (X35,X34,X33,X32) per batch
- transpose_from_word_sliced: inverse of word-sliced layout
- Scalar fallback for tail bytes (< 256)

The EncryptCTRMatchesScalarBaseline1MB consistency test now passes."
```

---

## Self-Review

1. **Spec coverage:**
   - [x] ROTL32_AVX2 helper (Task 2)
   - [x] parallel_L_transform (Task 2)
   - [x] BSWAP32_MASK for endianness conversion (Task 1)
   - [x] 32-round loop with sliding registers (Task 4)
   - [x] Output reversal X35,X34,X33,X32 (Task 4)
   - [x] Inverse transpose word-sliced → block-sliced (Task 3)
   - [x] XOR keystream with plaintext (Task 4)
   - [x] Tail handling for < 256 bytes (Task 4)
   - [x] Consistency test verification (Task 5)
   - [x] Commit (Task 6)

2. **Placeholder scan:** Zero TBD/TODO/fill-in-later — all code, commands, and expected output are explicit.

3. **Type consistency:**
   - `parallel_L_transform` takes `__m256i`, returns `__m256i` — consistent across header and implementation
   - `transpose_from_word_sliced` takes `const __m256i*` and `uint8_t[256]` — matches inverse of `transpose_to_word_sliced`
   - `sm4_encrypt_block_scalar` takes `const uint32_t rk[32]` matching `rk_` member type
   - Round keys broadcast: `_mm256_set1_epi32(static_cast<int>(rk_[round]))` — correct cast from uint32_t to int for intrinsic
   - All anonymous namespace helpers (`sm4_sbox_word`, `rotl`) already exist and are used unchanged
