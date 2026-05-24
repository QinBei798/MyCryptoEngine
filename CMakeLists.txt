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

FetchContent_Declare(
    zstd
    GIT_REPOSITORY https://github.com/facebook/zstd.git
    GIT_TAG        v1.5.5
    SOURCE_SUBDIR  build/cmake
)
FetchContent_MakeAvailable(zstd)

include_directories(${CMAKE_SOURCE_DIR}/include)

find_package(OpenSSL REQUIRED)

add_library(sm4_core STATIC
    src/sm4_standard.cpp
)

add_library(sm4_avx2 STATIC
    src/sm4_avx2.cpp
)

add_library(crypto_utils STATIC
    src/crypto_utils.cpp
)
target_link_libraries(crypto_utils OpenSSL::Crypto)

add_library(compress_utils STATIC
    src/compress_utils.cpp
)
target_include_directories(compress_utils PRIVATE ${zstd_SOURCE_DIR}/lib)
target_link_libraries(compress_utils libzstd_static)

add_library(secure_chunk_pipeline STATIC
    src/secure_chunk_pipeline.cpp
)

add_library(sm4z_archiver STATIC
    src/sm4z_archiver.cpp
)

add_executable(sm4_showcase src/main.cpp)
target_link_libraries(sm4_showcase sm4_avx2 sm4_core crypto_utils compress_utils)

enable_testing()
add_subdirectory(tests)
add_subdirectory(benchmarks)
