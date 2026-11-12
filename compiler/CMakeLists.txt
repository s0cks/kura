add_library(kura-compiler STATIC)
add_subdirectory(frontend/syntax)
add_subdirectory(frontend)
add_subdirectory(backend)
add_subdirectory(dom)

option(KURA_ADD_TRACING_SUPPORT "" ON)
if(KURA_ADD_TRACING_SUPPORT)
  target_compile_definitions(kura-compiler PUBLIC KURA_TRACING_ENABLED)
endif()

target_link_libraries(kura-compiler
  PUBLIC kurart)

add_library(kura-heap-abi-check OBJECT
  abi_check.cc)
add_dependencies(kura-heap kura-heap-abi-check)
