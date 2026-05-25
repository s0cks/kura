configure_file(${CMAKE_CURRENT_SOURCE_DIR}/kura.h.in ${CMAKE_CURRENT_BINARY_DIR}/kura.h)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

set(LLVM_LINK_LLVM_DYLIB ON) 
find_package(LLVM 22.1 REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

llvm_map_components_to_libnames(llvm_libs 
  x86 
  asmparser 
  support
  core
)

find_package(yoga CONFIG REQUIRED)

add_library(kura-runtime SHARED
  compiler.cc
  dom.cc
  dom_compiler.cc
  expr.cc
  layout.cc
  lexer.cc
  parser.cc
  source_pos.cc
  token.cc
  type.cc
)

target_compile_definitions(kura-runtime
  PUBLIC ${LLVM_DEFINITIONS}
)
target_include_directories(kura-runtime
  PUBLIC ${LLVM_INCLUDE_DIRS}
)
target_link_libraries(kura-runtime
  PUBLIC LLVM
         yogacore
)

add_subdirectory(compiler/)
add_subdirectory(debugger/)
