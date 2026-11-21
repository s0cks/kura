# The flexbox layout kernel. It depends on nothing else in Kura (not even LLVM),
# so it is its own library:
# the compiler links it in for the generated reflow code to call,
# and tests/layout tests it alone.

add_library(kura-layout STATIC layout.cc)
set_target_properties(kura-layout PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(kura-layout PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(KURA_BUILD_BENCHMARKS)
  find_package(benchmark CONFIG REQUIRED)

  add_executable(kura-layout-benchmarks benchmark.cc)
  target_link_libraries(kura-layout-benchmarks
    PRIVATE kura-layout
            benchmark::benchmark
            benchmark::benchmark_main)
endif()
