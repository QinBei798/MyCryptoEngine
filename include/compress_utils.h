#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

std::vector<uint8_t> compress_zstd(const std::vector<uint8_t>& src);

std::vector<uint8_t> decompress_zstd(const std::vector<uint8_t>& src,
                                     size_t original_size);
