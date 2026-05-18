# AVX2 向量化计数器生成 — 微步实施计划

**状态:** RED → GREEN
**范围:** 仅实现 `SM4AVX2::generate_16_counters`，禁止涉及 S-box、轮函数、CTR 加密

---

## 架构概览

```
generate_16_counters(state_matrix, initial_counter)
  │
  ├─ 1. 溢出检测 (Carry Detection)
  │     提取低 64 位 BE → BSWAP64 → LE
  │     若 lo_le + 15 < lo_le → Slow-Path (进位溢出到高 64 位)
  │
  ├─ 2. Fast-Path (不溢出, 99.99%)
  │     AVX2 向量化: 8 路并行生成 16 个计数器
  │     存入临时缓冲区 temp[256] (16 个连续的 16-byte BE 块)
  │
  ├─ 3. Slow-Path (溢出)
  │     标量循环: 老老实实生成 16 个 BE 块
  │     存入 temp[256]
  │
  └─ 4. 转置 (Transpose)
        temp[256] (block-sliced) → state_matrix[8] (word-sliced)
        使用 _mm256_i32gather_epi32 × 8
```

---

## 微步检查清单

### Step 0: 准备工作

- [ ] 0.1 在匿名命名空间中定义 `alignas(32) constexpr uint8_t BSWAP64_MASK[32]`
  - 每个 64-bit 元素内字节反转: `{7,6,5,4,3,2,1,0, 15,14,13,12,11,10,9,8, 7,6,5,4,3,2,1,0, 15,14,13,12,11,10,9,8}`
  - 用于 `_mm256_shuffle_epi8` 实现 4 路 64-bit BSWAP

- [ ] 0.2 在匿名命名空间中定义辅助函数 `inline uint64_t bswap64(uint64_t x)`
  - 标量 BSWAP，用于溢出检测阶段的单个 64-bit 值翻转

### Step 1: 溢出检测

- [ ] 1.1 从 `initial_counter[16]` 提取低 64 位 (BE)
  - `lo_be = *(uint64_t*)(initial_counter + 8)` — **高危**: x86 是小端序，直接读取 BE 字节会得到错误的值
  - **正确做法**: `lo_be = (uint64_t)initial_counter[8] << 56 | (uint64_t)initial_counter[9] << 48 | ... | initial_counter[15]`
  - 或使用 `bswap64(*(uint64_t*)(initial_counter + 8))` — 先读 LE 再翻转

- [ ] 1.2 转换为 LE 并进行溢出判断
  - `lo_le = bswap64(lo_be)`
  - `if (lo_le + 15 < lo_le) → Slow-Path`

- [ ] 1.3 同样提取高 64 位 `hi_le = bswap64(hi_be)`
  - `hi_be` 从 `initial_counter[0..7]` BE 提取

### Step 2: Fast-Path (AVX2 向量化)

- [ ] 2.1 加载 BSWAP64_MASK
  - `__m256i bswap_mask = _mm256_load_si256((__m256i*)BSWAP64_MASK)`

- [ ] 2.2 构造基向量 base = `_mm256_set_epi64x(lo_le, hi_le, hi_le, lo_le)`
  - **高危 `_mm256_set_epi64x` 参数顺序**: `(e3, e2, e1, e0)` → 内存布局 `[e0, e1, e2, e3]`
  - e0 = lo_le (ctr0 低 64 位 LE)
  - e1 = hi_le (ctr0 高 64 位 LE)
  - e2 = hi_le (ctr1 高 64 位 LE, 不变)
  - e3 = lo_le (ctr1 低 64 位 LE, 需+1)
  - base 存储后: `[hi_le, lo_le, hi_le, lo_le]`? 不对...
  
  **实际验证**:
  `_mm256_set_epi64x(A, B, C, D)` → 寄存器 `[D@0:63, C@64:127, B@128:191, A@192:255]`
  存储到内存: D 在最低地址, A 在最高地址
  我们需要: A+0..7=hi_be_0, A+8..15=lo_be_0, A+16..23=hi_be_1, A+24..31=lo_be_1 (BE 格式)
  
  在 LE 阶段: 我们需要 A+0..7=hi_le_0, A+8..15=lo_le_0+0, A+16..23=hi_le_1, A+24..31=lo_le_1+1
  但 BSWAP64 会反转每个 64-bit 元素内的字节，所以 BSWAP64 后的 layout:
  - A+0..7 = BSWAP64(element0) = BSWAP64(lo_le_0) = lo_be_0 ← 需要的是 hi_be_0!
  
  **关键洞察**: BSWAP64 只翻转每个 64-bit 元素的字节，不改变 64-bit 元素之间的顺序。所以要得到正确的 BE 内存布局，我们需要在 LE 阶段就把 hi_le 和 lo_le 放在正确的位置:
  - BSWAP64 后 A+0..7: hi_be_0 → LE 阶段 element0 = hi_le_0
  - BSWAP64 后 A+8..15: lo_be_0 → LE 阶段 element1 = lo_le_0
  - BSWAP64 后 A+16..23: hi_be_1 → LE 阶段 element2 = hi_le_1
  - BSWAP64 后 A+24..31: lo_be_1 → LE 阶段 element3 = lo_le_1
  
  所以 LE 布局应该是: `[hi_le_0, lo_le_0, hi_le_1, lo_le_1]`
  element0 = hi_le_0 → e0 = hi_le
  element1 = lo_le_0 → e1 = lo_le
  element2 = hi_le_1 → e2 = hi_le
  element3 = lo_le_1 → e3 = lo_le
  
  `base = _mm256_set_epi64x(lo_le, hi_le, lo_le, hi_le)`
  
  **此映射关系极其反直觉，必须逐字节验证！**

- [ ] 2.3 8 路并行加法生成 16 个计数器 (存入 temp[256])
  - 循环 i = 0..7:
    - offset_i = `_mm256_set_epi64x(2*i+1, 0, 2*i, 0)`
    - sum = `_mm256_add_epi64(base, offset_i)`
    - be = `_mm256_shuffle_epi8(sum, bswap_mask)` — 4 路 64-bit BSWAP
    - `_mm256_store_si256((__m256i*)(temp + 64*i), be)`
  - 验证: temp[0..15] = ctr0 (BE), temp[16..31] = ctr1 (BE), ...

- [ ] 2.4 在进入 Fast-Path 之前，先验证 temp 中 ctr0 == initial_counter
  - 可在 Debug 构建中加断言

### Step 3: Slow-Path (标量回退)

- [ ] 3.1 使用已验证的标量大端序递增逻辑
  - `uint8_t ctr[16]; memcpy(ctr, initial_counter, 16)`
  - for i = 0..15: memcpy(temp + i*16, ctr, 16); ctr_increment(ctr)

### Step 4: 转置到 Word-Sliced 格式

- [ ] 4.1 对于 batch ∈ {0, 1}, word ∈ {0, 1, 2, 3}:
  - 构造 gather 索引: `_mm256_setr_epi32(0, 16, 32, 48, 64, 80, 96, 112)`
  - `state_matrix[batch*4 + word] = _mm256_i32gather_epi32((int*)(temp + batch*128 + word*4), index, 4)`

---

## 禁止事项

- [ ] 不实现 S-box 查表
- [ ] 不实现轮函数 (L, T, T')
- [ ] 不修改 encrypt_ctr
- [ ] 不修改测试文件
- [ ] 不修改 CMakeLists.txt
- [ ] 不修改头文件

---

## 验证标准

- [ ] ctest -R Counter 全部 GREEN (PASS)
- [ ] 无 SegFault
- [ ] 无编译警告
- [ ] ctest -E Counter (其他 12 个测试) 无回归

---

## `_mm256_set_epi64x` 高危映射表

| 参数位置 | 对应元素 | 存储地址 | 我们的用法 |
|---------|---------|---------|-----------|
| e3 (第1参数) | 最高 64 位 | A+24..31 | lo_le (ctr1 低半, 需+1) |
| e2 (第2参数) | 第2高 64 位 | A+16..23 | hi_le (ctr1 高半, 不变) |
| e1 (第3参数) | 第2低 64 位 | A+8..15 | lo_le (ctr0 低半, 需+0) |
| e0 (第4参数) | 最低 64 位 | A+0..7 | hi_le (ctr0 高半, 不变) |

BSWAP64 后: [hi_be_0, lo_be_0, hi_be_1, lo_be_1] = 正确的 BE 内存布局

**反例 (错误):**
若写成 `_mm256_set_epi64x(hi_le, lo_le, hi_le, lo_le)`:
- BSWAP64 后: [lo_be, hi_be, lo_be, hi_be] ← 高低半颠倒！

**偏移量映射:**
`offset_i = _mm256_set_epi64x(2*i+1, 0, 2*i, 0)`
- e3 (lo_le 位置) += 2*i+1 → ctr_{2i+1} 的低半
- e1 (lo_le 位置) += 2*i → ctr_{2i} 的低半
- e2, e0 (hi_le 位置) += 0 → 高半不变
