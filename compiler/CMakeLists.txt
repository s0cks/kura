add_library(kura-compiler STATIC)
add_subdirectory(frontend/syntax)
add_subdirectory(frontend)
add_subdirectory(backend)
add_subdirectory(dom)

target_link_libraries(kura-compiler
  PUBLIC kurart)

add_library(kura-heap-abi-check OBJECT
  abi_check.cc)
add_dependencies(kura-heap kura-heap-abi-check)
