find_package(benchmark CONFIG REQUIRED)

add_library(kura-gl-renderer-benchmark-core STATIC
    gl_fixture.cc)
target_link_libraries(kura-gl-renderer-benchmark-core
  PUBLIC kura-renderer-gl
         kura-compiler
         kura-renderer-base
         kura-text
         PkgConfig::EPOXY
         benchmark::benchmark)
target_compile_definitions(kura-gl-renderer-benchmark-core
  PUBLIC
    KURA_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples")

function(add_gl_renderer_benchmark name sources)
  add_executable(kura-gl-renderer-${name}-benchmark
    ${sources})
  target_link_libraries(kura-gl-renderer-${name}-benchmark
    PRIVATE kura-gl-renderer-benchmark-core
            benchmark::benchmark_main)
endfunction()

add_gl_renderer_benchmark(gpu benchmark_gpu.cc)
