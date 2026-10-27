configure_file(${CMAKE_CURRENT_SOURCE_DIR}/kura.h.in ${CMAKE_CURRENT_BINARY_DIR}/kura.h)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

include(Antlr4)

set(GRAMMAR_NAME "Kura")
list(APPEND GRAMMAR_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/${GRAMMAR_NAME}Parser.g4"
  "${CMAKE_CURRENT_SOURCE_DIR}/${GRAMMAR_NAME}Lexer.g4")

set(GEN_SOURCES
  "${CMAKE_CURRENT_BINARY_DIR}/${GRAMMAR_NAME}Lexer.cpp"
  "${CMAKE_CURRENT_BINARY_DIR}/${GRAMMAR_NAME}Parser.cpp"
)

add_custom_command(
  OUTPUT ${GEN_SOURCES}
  COMMAND ${ANTLR} ${ANTLR_ARGS} ${GRAMMAR_SOURCES}
  DEPENDS ${GRAMMAR_SOURCES}
  COMMENT "Generating c++ parser from ${GRAMMAR_NAME}.g4"
  VERBATIM
)

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
  ${GEN_SOURCES}
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
         antlr::antlr4
         glm::glm
)

add_subdirectory(dom)
add_subdirectory(renderer)
add_subdirectory(frontend)
add_subdirectory(backend)
add_subdirectory(runtime)
add_subdirectory(compiler)

if(KURA_BUILD_DEBUGGER)
  add_subdirectory(debugger)
endif()
