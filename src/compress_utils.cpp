#include "compress_utils.h"

#include <cstdint>
#include <stdexcept>
#include <vector>

#include <zstd.h>

std::vector<uint8_t> compress_zstd(const std::vector<uint8_t>& src) {
    size_t bound = ZSTD_compressBound(src.size());
    std::vector<uint8_t> dst(bound);

    size_t actual = ZSTD_compress(dst.data(), dst.size(),
                                  src.data(), src.size(),
                                  3); // level 3 — fast default

    if (ZSTD_isError(actual))
        throw std::runtime_error(std::string("ZSTD_compress failed: ") +
                                 ZSTD_getErrorName(actual));

    dst.resize(actual);
    return dst;
}

std::vector<uint8_t> decompress_zstd(const std::vector<uint8_t>& src,
                                     size_t original_size) {
    std::vector<uint8_t> dst(original_size);

    size_t actual = ZSTD_decompress(dst.data(), dst.size(),
                                    src.data(), src.size());

    if (ZSTD_isError(actual))
        throw std::runtime_error(std::string("ZSTD_decompress failed: ") +
                                 ZSTD_getErrorName(actual));

    if (actual != original_size)
        throw std::runtime_error("ZSTD_decompress: size mismatch");

    return dst;
}
