configure_file(${CMAKE_CURRENT_SOURCE_DIR}/kura.h.in ${CMAKE_CURRENT_BINARY_DIR}/kura.h)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR})


add_library(kura-compiler SHARED
  function.cc
  unary_op.cc
  binary_op.cc
  c_import.cc
  local_scope.cc
  module.cc
  object.cc
  property.cc
  record.cc
  type.cc
  view.cc
)

target_compile_definitions(kura-compiler
  PUBLIC ${LLVM_DEFINITIONS}
         KURA_CLANG_EXECUTABLE="${KURA_CLANG_EXECUTABLE}"
  PRIVATE KURA_CLANG_RESOURCE_DIR="${KURA_CLANG_RESOURCE_DIR}"
)
target_include_directories(kura-compiler
  PUBLIC ${LLVM_INCLUDE_DIRS}
         ${CLANG_INCLUDE_DIRS}
         ${CMAKE_CURRENT_SOURCE_DIR}
         ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(kura-compiler
  PUBLIC LLVM
         clang-cpp
         PkgConfig::XXHASH
         glm::glm
         kura-syntax
         kura-layout
)

add_subdirectory(layout)
add_subdirectory(heap)
add_subdirectory(dom)
add_subdirectory(backend)
add_subdirectory(frontend/syntax)
add_subdirectory(frontend)
add_subdirectory(runtime)

add_subdirectory(renderer)

add_subdirectory(compiler)

if(KURA_BUILD_DEBUGGER)
  add_subdirectory(debugger)
endif()
