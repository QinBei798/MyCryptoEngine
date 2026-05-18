add_test( SM4AVX2SboxTest.ParallelSbox16MatchesScalarGroundTruth /mnt/d/MyCryptoEngine/build/tests/test_sm4_avx2_sbox [==[--gtest_filter=SM4AVX2SboxTest.ParallelSbox16MatchesScalarGroundTruth]==] --gtest_also_run_disabled_tests)
set_tests_properties( SM4AVX2SboxTest.ParallelSbox16MatchesScalarGroundTruth PROPERTIES WORKING_DIRECTORY /mnt/d/MyCryptoEngine/build/tests SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==])
set( test_sm4_avx2_sbox_TESTS SM4AVX2SboxTest.ParallelSbox16MatchesScalarGroundTruth)
