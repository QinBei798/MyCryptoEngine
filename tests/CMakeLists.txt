add_executable(test_sm4_standard
    test_sm4_standard.cpp
)

target_link_libraries(test_sm4_standard
    sm4_core
    GTest::gtest
    GTest::gtest_main
)

add_executable(test_sm4_avx2_consistency
    test_sm4_avx2_consistency.cpp
)

target_link_libraries(test_sm4_avx2_consistency
    sm4_avx2
    sm4_core
    GTest::gtest
    GTest::gtest_main
)

add_executable(test_sm4_avx2_counter
    test_sm4_avx2_counter.cpp
)

target_link_libraries(test_sm4_avx2_counter
    sm4_avx2
    GTest::gtest
    GTest::gtest_main
)

add_executable(test_sm4_avx2_sbox
    test_sm4_avx2_sbox.cpp
)

target_link_libraries(test_sm4_avx2_sbox
    sm4_avx2
    GTest::gtest
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(test_sm4_standard)
gtest_discover_tests(test_sm4_avx2_consistency)
gtest_discover_tests(test_sm4_avx2_counter)
gtest_discover_tests(test_sm4_avx2_sbox)

add_executable(test_secure_chunk_pipeline
    test_secure_chunk_pipeline.cpp
)

target_link_libraries(test_secure_chunk_pipeline
    secure_chunk_pipeline
    compress_utils
    sm4_avx2
    sm4_core
    crypto_utils
    libzstd_static
    GTest::gtest
    GTest::gtest_main
)

gtest_discover_tests(test_secure_chunk_pipeline)

add_executable(test_sm4z_archiver
    test_sm4z_archiver.cpp
)

target_link_libraries(test_sm4z_archiver
    sm4z_archiver
    secure_chunk_pipeline
    compress_utils
    sm4_avx2
    sm4_core
    crypto_utils
    libzstd_static
    OpenSSL::Crypto
    GTest::gtest
    GTest::gtest_main
)

gtest_discover_tests(test_sm4z_archiver)
