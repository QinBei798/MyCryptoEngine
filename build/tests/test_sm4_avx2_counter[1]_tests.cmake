add_test( SM4AVX2CounterTest.Generate16CountersMatchesScalarGroundTruth /mnt/d/MyCryptoEngine/build/tests/test_sm4_avx2_counter [==[--gtest_filter=SM4AVX2CounterTest.Generate16CountersMatchesScalarGroundTruth]==] --gtest_also_run_disabled_tests)
set_tests_properties( SM4AVX2CounterTest.Generate16CountersMatchesScalarGroundTruth PROPERTIES WORKING_DIRECTORY /mnt/d/MyCryptoEngine/build/tests SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==])
set( test_sm4_avx2_counter_TESTS SM4AVX2CounterTest.Generate16CountersMatchesScalarGroundTruth)
