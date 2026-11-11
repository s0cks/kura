find_package(benchmark CONFIG REQUIRED)

add_library(kura-benchmark INTERFACE)
target_link_libraries(kura-benchmark
  INTERFACE benchmark::benchmark
            benchmark::benchmark_main)

add_subdirectory(parser)
