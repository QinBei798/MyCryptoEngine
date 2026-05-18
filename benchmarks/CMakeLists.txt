add_executable(sm4_benchmark
    sm4_benchmark.cpp
)

target_link_libraries(sm4_benchmark
    sm4_avx2
    sm4_core
    benchmark::benchmark
)
