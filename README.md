<div align="center">

# MyCryptoEngine

**SM4-CTR 国密存储加密引擎 · AVX2 SIMD 硬件加速**

[![C++17](https://img.shields.io/badge/C%2B%2B-17-%2300599C?style=flat&logo=c%2B%2B)](https://en.cppreference.com/w/cpp/17)
[![CMake](https://img.shields.io/badge/CMake-3.20-%23064F8C?style=flat&logo=cmake)](https://cmake.org/)
[![AVX2](https://img.shields.io/badge/SIMD-AVX2-%23ED1C24?style=flat&logo=intel)](https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html)
[![TDD](https://img.shields.io/badge/method-TDD-%232DBA4E?style=flat)](https://en.wikipedia.org/wiki/Test-driven_development)
[![License](https://img.shields.io/badge/license-MIT-%2397CA00?style=flat)](LICENSE)
[![Tests](https://img.shields.io/badge/tests-14%2F14%20passed-%232DBA4E?style=flat)]()

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
- **安全文件存储 V2** — `SM4X` 加密容器，PBKDF2-SM3 口令派生密钥 (100k iter) + HMAC-SM3 完整性校验 (Encrypt-then-MAC)，防篡改/防密码错误

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
- **依赖:** OpenSSL ≥ 3.0 (libssl-dev)，用于 PBKDF2-SM3 密钥派生与 HMAC-SM3 完整性校验

### 构建 & 测试

```bash
git clone <repo-url> && cd MyCryptoEngine
mkdir build && cd build

# 配置 (自动拉取 Google Test 和 Google Benchmark)
cmake .. -DCMAKE_BUILD_TYPE=Release

# 编译全部目标
cmake --build . -j$(nproc)

# 运行测试套件 (14 项)
ctest -R "SM4" --output-on-failure
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
  [5]  Encrypt File — secure storage to .sm4x container
  [6]  Decrypt File — extract from .sm4x container
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
| **[5] Encrypt File** | 口令加密任意文件 → SM4X V2 容器 | PBKDF2-SM3 (100k iter) 口令派生密钥，随机 Salt + IV，HMAC-SM3 签名 (Encrypt-then-MAC) |
| **[6] Decrypt File** | 口令解密 `.sm4x` → 完整性校验 + 还原 | `CRYPTO_memcmp` 恒定时间 HMAC 比对，错误密码/篡改文件立即拦截 (FATAL) |

### Hex 解析器容错示例

```
  Enter hex ciphertext:  0a:f3:2c 4e 0x7b  A1B2C3
  [+] Parsed 7 bytes of ciphertext.
  [+] Decrypted Plaintext: Hello, World!
```

支持空格分隔、冒号分隔、`0x` 前缀、大小写混合、无分隔连续串 — 均可正确解析。

### SM4X V2 安全文件格式

V2 格式引入 PBKDF2 口令派生与 HMAC-SM3 完整性校验，具备防篡改能力：

```
┌──────────┬───────────────┬──────────────┬──────────────────┬─────────────────┐
│ SM4X (4B)│  Salt (16B)   │  IV (16B)    │  Ciphertext (N)  │  HMAC-SM3 (32B) │
└──────────┴───────────────┴──────────────┴──────────────────┴─────────────────┘
           ←──────────── Header (36B) ────────────→                    ← Footer →
           ←───────────────── HMAC covers this ───────────────────→
```

| 字段 | 偏移 | 大小 | 描述 |
|------|------|------|------|
| Magic Bytes | 0 | 4 B | 固定 `SM4X` (0x53 0x4D 0x34 0x58)，格式校验 |
| Salt | 4 | 16 B | 随机 Salt，用于 PBKDF2-SM3 密钥派生 (100,000 iterations) |
| File IV | 20 | 16 B | 该文件专属随机 IV，用于 SM4-CTR |
| Ciphertext | 36 | N B | SM4-CTR 密文，与明文等长 |
| HMAC-SM3 | 36+N | 32 B | Encrypt-then-MAC，恒定时间比对防时序攻击 |

派生总长度 48 字节：前 16 B 用作 SM4 密钥，后 32 B 用作 HMAC-SM3 密钥。
解密时 HMAC 校验失败（错误密码或篡改）立即拒绝解密，不写入任何文件。

---

## 🛠️ 架构

```
MyCryptoEngine/
├── include/
│   ├── sm4_standard.h        # SM4Standard 标量引擎 (基线实现)
│   ├── sm4_avx2.h            # SM4AVX2 SIMD 加速引擎
│   └── crypto_utils.h        # PBKDF2-SM3 密钥派生 + HMAC-SM3 声明
├── src/
│   ├── sm4_standard.cpp      # 标量: 逐块 CTR 加密 + 大端序进位
│   ├── sm4_avx2.cpp          # AVX2: 16路并行 32 轮 SM4 流水线
│   ├── crypto_utils.cpp      # PBKDF2-SM3 + HMAC-SM3 (OpenSSL EVP)
│   └── main.cpp              # 交互式 REPL 终端 + SM4X V2 安全存储
├── tests/
│   ├── test_sm4_standard.cpp       # 国密标准向量 (加密/解密/CTR)
│   ├── test_sm4_avx2_consistency.cpp   # 1MB 标量 vs AVX2 逐字节比对
│   ├── test_sm4_avx2_counter.cpp      # 计数器生成验证
│   ├── test_sm4_avx2_sbox.cpp         # 并行 S 盒验证
│   └── test_sm4_avx2_align.cpp        # 内存对齐安全测试
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
- **最小外部依赖** — 核心 SM4 零依赖 (仅 AVX2 intrinsic)；安全存储模块依赖 OpenSSL 3.0+ 提供 PBKDF2/HMAC-SM3
- **微观提交** — 每个 Phase 独立 plan → 实现 → review → commit

---

<div align="center">
<sub>Built with ⚡ AVX2 intrinsics · C++17 · Strict TDD · GM/T 0002-2012</sub>
</div>
