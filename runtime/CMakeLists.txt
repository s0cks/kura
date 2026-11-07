configure_file(${CMAKE_CURRENT_SOURCE_DIR}/kura.h.in ${CMAKE_CURRENT_BINARY_DIR}/kura.h)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_library(kurart SHARED
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
  log.cc
)

target_compile_definitions(kurart
  PUBLIC ${LLVM_DEFINITIONS}
         KURA_CLANG_EXECUTABLE="${KURA_CLANG_EXECUTABLE}"
  PRIVATE KURA_CLANG_RESOURCE_DIR="${KURA_CLANG_RESOURCE_DIR}"
)
target_include_directories(kurart
  PUBLIC ${LLVM_INCLUDE_DIRS}
         ${CLANG_INCLUDE_DIRS}
         ${CMAKE_CURRENT_SOURCE_DIR}
         ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(kurart
  PUBLIC LLVM
         clang-cpp
         PkgConfig::XXHASH
         glm::glm
         kura-heap
         kura-syntax
         kura-layout
)
