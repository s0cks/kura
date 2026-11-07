add_executable(kura-fuzzing target.cc)

target_compile_options(kura-fuzzing
  PRIVATE -fsanitize=fuzzer,address,undefined
          -fno-omit-frame-pointer
          -g
)

target_link_options(kura-fuzzing
  PRIVATE -fsanitize=fuzzer,address,undefined)

target_link_libraries(kura-fuzzing
  PRIVATE kura-compiler)
