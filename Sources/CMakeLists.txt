configure_file(${CMAKE_CURRENT_SOURCE_DIR}/kura.h.in ${CMAKE_CURRENT_BINARY_DIR}/kura.h)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

include(Antlr4)

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

add_library(kura-runtime SHARED
  compiler.cc
  dom.cc
  dom_compiler.cc
  expr.cc
  expr_builder.cc
  layout.cc
  parser.cc
  type.cc
  ${GEN_SOURCES}
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
         antlr::antlr4
)

add_subdirectory(compiler/)
add_subdirectory(debugger/)
