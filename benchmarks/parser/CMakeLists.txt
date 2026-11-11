add_executable(kura-parser-benchmark
  main.cc)
target_link_libraries(kura-parser-benchmark
  PUBLIC kura-benchmark
         kura-compiler)
