<div align="center">

# MyCryptoEngine

**SM4-CTR 国密存储加密引擎 · AVX2 SIMD 硬件加速**

[![C++17](https://img.shields.io/badge/C%2B%2B-17-%2300599C?style=flat&logo=c%2B%2B)](https://en.cppreference.com/w/cpp/17)
[![CMake](https://img.shields.io/badge/CMake-3.20-%23064F8C?style=flat&logo=cmake)](https://cmake.org/)
[![AVX2](https://img.shields.io/badge/SIMD-AVX2-%23ED1C24?style=flat&logo=intel)](https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html)
[![TDD](https://img.shields.io/badge/method-TDD-%232DBA4E?style=flat)](https://en.wikipedia.org/wiki/Test-driven_development)
[![License](https://img.shields.io/badge/license-MIT-%2397CA00?style=flat)](LICENSE)
[![Tests](https://img.shields.io/badge/tests-99%2F99%20passed-%232DBA4E?style=flat)]()
[![Multi--thread](https://img.shields.io/badge/parallel-std%3A%3Aasync-%23FF6F00?style=flat)](https://en.cppreference.com/w/cpp/thread/async)
[![Zstd](https://img.shields.io/badge/compress-Zstd%201.5.5-%23E64A19?style=flat)](https://github.com/facebook/zstd)

</div>

---

## ✨ 核心特性

- **16 路并行加密** — 单次 AVX2 调用同时处理 **16 个 SM4 数据块**（2 批次 × 8 路），256 字节/轮
- **Gather 并行 S 盒** — 基于 `_mm256_i32gather_epi32` 的字节切片 τ 变换，4 条指令完成 16 字并查表
- **SIMD L 线性层** — 8 路并行循环移位 `L(B) = B ⊕ (B⋘2) ⊕ (B⋘10) ⊕ (B⋘18) ⊕ (B⋘24)`
- **双路径大端序计数器** — Fast-Path SIMD 向量加法（覆盖 99.99% 场景）+ Slow-Path 标量溢出保护
- **词切片数据布局 (SoA)** — 8 × `__m256i` 寄存器矩阵，AoS ↔ SoA 转置，最优 SIMD 访存模式
- **严苛内存对齐** — 所有 AVX2 缓冲区 `alignas(32)` 对齐，`_mm_malloc` 分配，零 SegFault
- **逐字节一致性验证** — 1MB CTR 加密输出与标量基线 `SM4Standard` 逐字节一致
- **GM/T 0002-2012 合规** — 全部国密标准测试向量通过
- **安全文件存储 V3** — `SM4Z` 加密容器，Zstd 压缩 + SM4-CTR 加密 + PBKDF2-SM3 密钥派生 (100k iter) + HMAC-SM3 完整性校验 (Encrypt-then-MAC)，【压缩 → 加密 → 存储】极速流水线
- **SM4ZArchiver 归档引擎** — 可变长度 `.sm4z` 容器格式，80B 全局头部 + N×16B 分块索引表 + 数据区，支持 HMAC 完整性校验与篡改/截断防御
- **狂暴模式 (Berserk Mode)** — `std::async` 多线程并行打包，4MB 宏块分割 → 工作线程并发处理 Zstd + SM4-AVX2 + HMAC，32 字节对齐缓冲区，主线程有序序列化保证确定性输出

---

## 📊 性能对比

> 测试平台：Intel × 16 Core @ 3187 MHz, L3 18 MiB | GCC `-O3 -mavx2` | 100MB 随机明文

| 实现 | 数据量 | 耗时 | 吞吐量 | 加速比 |
|------|--------|------|--------|--------|
| `SM4Standard` (标量) | 100 MB | 1489 ms | **67.1 MiB/s** | 1.0× |
| `SM4AVX2` (SIMD) | 100 MB | 434 ms | **230.2 MiB/s** | **3.4×** |

```
SM4Standard  ██████████████████████████████████████████████████  67 MiB/s
SM4AVX2      ██████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████ 230 MiB/s
                                                         ↑ 3.4× 加速
```

---

## 🚀 快速开始

### 环境要求

- **编译器:** GCC 8+ / Clang 10+ (需 `-mavx2` 支持)
- **CMake:** ≥ 3.20
- **C++ 标准:** C++17
- **依赖:** OpenSSL ≥ 3.0 (libssl-dev) 用于 PBKDF2/HMAC-SM3，Zstd 1.5.5 通过 CMake FetchContent 自动拉取

### 构建 & 测试

```bash
git clone <repo-url> && cd MyCryptoEngine
mkdir build && cd build

# 配置 (自动拉取 Google Test, Google Benchmark, Zstd)
cmake .. -DCMAKE_BUILD_TYPE=Release

# 编译全部目标
cmake --build . -j$(nproc)

# 运行测试套件 (99 项)
ctest --output-on-failure
```

### 运行性能压测

```bash
# 直接运行 benchmark 二进制
./benchmarks/sm4_benchmark

# 输出示例:
# BM_SM4_Standard_100MB   1489 ms   67.1 MiB/s
# BM_SM4_AVX2_100MB        434 ms  230.2 MiB/s
```

---

## 💻 交互式控制台 (Interactive CLI)

开箱即用的 **REPL 终端应用**，集加解密演示、Hex 解析与现场性能对决于一体。一次编译，即刻体验 SM4-AVX2 的全部能力。

### 启动

```bash
./build/sm4_showcase
```

程序启动后输出 ASCII Art 标题，并自动生成**会话级 Key + IV**（固定复用，方便跨操作对比）：

```text
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

╔════════════════════════════════════════════════════════════════════╗
║  SESSION PARAMETERS                                             ║
╚════════════════════════════════════════════════════════════════════╝

  Session Key  (16 B): 82 2d 22 36 b0 19 5b 3b  ad a7 09 1c a0 6a 81 df
  Session IV   (16 B): ea 4f ee ba a5 80 1d 97  e1 39 97 69 d3 8c 69 84

──────────────────────────────────────────────────────────────────────
  [1]  Encrypt — custom plaintext string
  [2]  Decrypt — hex ciphertext back to plaintext
  [3]  Benchmark — SM4Standard vs SM4AVX2 live speedrun
  [4]  Exit
  [5]  Encrypt File — secure storage to .sm4z container
  [6]  Decrypt File — extract from .sm4z container
──────────────────────────────────────────────────────────────────────
  Choice >
```

### 菜单功能

| 选项 | 功能 | 亮点 |
|------|------|------|
| **[1] Encrypt** | 输入任意文本 → SM4-CTR 加密 | 含空格的整行文本支持，输出带 ASCII 边栏的 Hex Dump，自动回环验证 |
| **[2] Decrypt** | 粘贴十六进制密文 → 还原明文 | **带容错的 Hex 自动解析器** — 自动忽略空格、冒号、`0x` 前缀；奇数长度/非法字符即时报错 |
| **[3] Benchmark** | 自定义数据量（1–4096 MB）→ 标量 vs AVX2 对决 | `_mm_malloc` 32 字节对齐分配，高精度 `std::chrono` 计时，加速比 + 吞吐量表，`memcmp` 逐字节一致性校验 |
| **[4] Exit** | 优雅退出 | — |
| **[5] Encrypt File** | 口令加密任意文件 → SM4Z V3 容器 | Zstd 压缩 → PBKDF2-SM3 密钥派生 → SM4-CTR 加密 (64KB 分块) → HMAC-SM3 签名，输出压缩比 |
| **[6] Decrypt File** | 口令解密 `.sm4z` → 校验 + 解压 + 还原 | HMAC-SM3 恒定时间比对 (Header + Chunk) → SM4-CTR 解密 → Zstd 解压恢复原始明文 |

### Hex 解析器容错示例

```
  Enter hex ciphertext:  0a:f3:2c 4e 0x7b  A1B2C3
  [+] Parsed 7 bytes of ciphertext.
  [+] Decrypted Plaintext: Hello, World!
```

支持空格分隔、冒号分隔、`0x` 前缀、大小写混合、无分隔连续串 — 均可正确解析。

### SM4Z V3 安全文件格式

V3 格式在 V2 基础上引入 **Zstd 无损压缩**与**可变长度分块索引**，实现「压缩 → 加密 → 存储」极速流水线：

```
┌──────────┬──────────┬──────────┬──────────┬──────────┬───────────┬───────────────┬──────────────────┬─────────────────┐
│ SM4Z (4B)│ Ver (4B) │Salt(16B) │ IV (16B) │Chunks(8B)│HMAC(32B) │ Index (N×16B) │ Ciphertext (Var)│ Chunk HMAC(32B) │
└──────────┴──────────┴──────────┴──────────┴──────────┴───────────┴───────────────┴──────────────────┴─────────────────┘
           ←────────────────── Global Header (80B) ──────────────────→ ← Chunk Index → ←──── Data Area (per chunk) ────→
           ←────────────────────────── HMAC covers header prefix ──────────────────────────────────────────────────────→
```

| 字段 | 偏移 | 大小 | 描述 |
|------|------|------|------|
| Magic Bytes | 0 | 4 B | 固定 `SM4Z` (0x53 0x4D 0x34 0x5A)，格式校验 |
| Version | 4 | 4 B | `0x00000003` (LE uint32_t) |
| Salt | 8 | 16 B | 随机 Salt，用于 PBKDF2-SM3 密钥派生 (100,000 iterations) |
| Base IV | 24 | 16 B | 该文件专属随机 IV，分块 IV 由 `base_iv ⊕ chunk_index` 派生 |
| Total Chunks | 40 | 8 B | 总分块数 (uint64_t LE) |
| Header MAC | 48 | 32 B | HMAC-SM3 覆盖前 48 字节头部前缀，解包时**先于索引解析**校验 |
| Chunk Index | 80 | N×16 B | 每条记录: `[FileOffset:8B][CompressedSize:4B][OriginalSize:4B]` (LE) |
| Ciphertext | var | var | Zstd → SM4-CTR 加密后的密文 |
| Chunk HMAC | var | 32 B | 每分块的 Encrypt-then-MAC，**先于解密**校验 |

**加密流水线:** `明文 → Zstd压缩(Level 3) → SM4-CTR加密 → HMAC-SM3签名 → .sm4z`

**解密流水线:** `.sm4z → HMAC校验 → SM4-CTR解密 → Zstd解压 → 原始明文`

派生总长度 48 字节：前 16 B 用作 SM4 密钥，后 32 B 用作 HMAC-SM3 密钥。
解密时 HMAC 校验失败（错误密码或篡改）立即拒绝，不写入任何文件。

---

### ⚡ 狂暴模式 (Berserk Mode)

当处理大文件（> 4 MB）时，`SM4ZArchiver::pack_archive` 自动启用 `std::async` 多线程并行流水线：

```
                     ┌─ 4MB Slice 0 ─► [Zstd → SM4-AVX2 → HMAC] ─┐
 Plaintext ─► Split ─┼─ 4MB Slice 1 ─► [Zstd → SM4-AVX2 → HMAC] ─┼─► Ordered Sequencer ─► .sm4z
                     └─ 4MB Slice N ─► [Zstd → SM4-AVX2 → HMAC] ─┘     (Main Thread)
                          ↑  std::async 并发 Worker 线程  ↑
```

| 阶段 | 职责 |
|------|------|
| **主线程** | 划分 4 MB 宏块，派发 `std::launch::async` 任务，按分块索引顺序等待 futures，组装索引表与数据区，写入磁盘 |
| **Worker 线程** | `std::aligned_alloc(32)` 分配 32 字节对齐缓冲区，独立实例化 `SecureChunkPipeline`，串行处理 64 KB 微块，`OPENSSL_cleanse` 擦除后返回 |
| **确定性保证** | 分块 IV 由 `base_iv ⊕ chunk_index` 派生（与调度顺序无关），主线程严格按宏块顺序收集结果 → 并行/串行输出 **逐字节一致** |

```cpp
SM4ZArchiver archiver;
archiver.set_num_threads(8);    // 启用 8 线程狂暴模式
archiver.pack_archive("data.bin", "data.sm4z", "password");
```

---

## 🛠️ 架构

```
MyCryptoEngine/
├── include/
│   ├── sm4_standard.h        # SM4Standard 标量引擎 (基线实现)
│   ├── sm4_avx2.h            # SM4AVX2 SIMD 加速引擎
│   ├── crypto_utils.h            # PBKDF2-SM3 密钥派生 + HMAC-SM3 声明
│   ├── compress_utils.h          # Zstd 无损压缩/解压声明
│   ├── secure_chunk_pipeline.h   # 分块加密流水线 (压缩→加密→签名)
│   └── sm4z_archiver.h           # SM4Z 归档引擎 (打包/解包 + 多线程)
├── src/
│   ├── sm4_standard.cpp          # 标量: 逐块 CTR 加密 + 大端序进位
│   ├── sm4_avx2.cpp              # AVX2: 16路并行 32 轮 SM4 流水线
│   ├── crypto_utils.cpp          # PBKDF2-SM3 + HMAC-SM3 (OpenSSL EVP)
│   ├── compress_utils.cpp        # Zstd 压缩/解压 (ZSTD_compress/ZSTD_decompress)
│   ├── secure_chunk_pipeline.cpp # 分块流水线: seal_chunk / open_chunk
│   ├── sm4z_archiver.cpp         # .sm4z 归档引擎: pack_archive / unpack_archive
│   └── main.cpp                  # 交互式 REPL 终端 + SM4Z V3 安全存储
├── tests/
│   ├── test_sm4_standard.cpp          # 国密标准向量 (加密/解密/CTR)
│   ├── test_sm4_avx2_consistency.cpp  # 1MB 标量 vs AVX2 逐字节比对
│   ├── test_sm4_avx2_counter.cpp      # 计数器生成验证
│   ├── test_sm4_avx2_sbox.cpp         # 并行 S 盒验证
│   ├── test_sm4_avx2_align.cpp        # 内存对齐安全测试
│   ├── test_secure_chunk_pipeline.cpp # 分块流水线往返 + 篡改检测 + 空输入
│   └── test_sm4z_archiver.cpp         # 多块打包/解包 + 头部篡改 + 截断 + 并行往返 + 确定性等价
├── benchmarks/
│   └── sm4_benchmark.cpp      # Google Benchmark: 10MB/100MB 吞吐量
├── docs/
│   └── superpowers/plans/     # 分阶段实现计划 (RED→GREEN TDD)
└── CMakeLists.txt             # 顶层构建: FetchContent + O3 + AVX2
```

### 加密流水线

```
Counter (BE) → BSWAP32 (BE→LE) → 32-round SM4 (2 batches × 8 blocks)
                                    ├ τ: Gather S-box (16-way parallel)
                                    └ L: SIMD rotate + XOR (8-way parallel)
              → Output Reversal → BSWAP32 (LE→BE) → Inverse Transpose
              → XOR Plaintext → Ciphertext
```

| 阶段 | 技术 | 描述 |
|------|------|------|
| **Counter** | `generate_16_counters` | 双路径 BE 128-bit 递增 (SIMD + Scalar 回退) |
| **τ (S-box)** | `parallel_sbox_16` | `_mm256_i32gather_epi32` 8 路并查 `SBOX_32` |
| **L (Linear)** | `parallel_L_transform` | `rotl32_avx2` 实现 4 次循环移位 + XOR |
| **Transpose** | AoS ↔ SoA | `_mm256_i32gather_epi32` + 手工组合, 最优 cache 局部性 |

---

## 📐 设计原则

- **TDD 驱动** — 每个核心函数先写 RED 测试，验证失败，再实现 GREEN，全程可追溯
- **最小外部依赖** — 核心 SM4 零依赖 (仅 AVX2 intrinsic)；安全存储模块依赖 OpenSSL 3.0+ (PBKDF2/HMAC-SM3) + Zstd 1.5.5 (无损压缩，FetchContent 自动拉取)；多线程使用标准库 `std::async` (零外部依赖)
- **微观提交** — 每个 Phase 独立 plan → 实现 → review → commit

---

<div align="center">
<sub>Built with ⚡ AVX2 intrinsics · C++17 · Strict TDD · GM/T 0002-2012</sub>
</div>
