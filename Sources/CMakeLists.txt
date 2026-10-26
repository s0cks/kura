configure_file(${CMAKE_CURRENT_SOURCE_DIR}/kura.h.in ${CMAKE_CURRENT_BINARY_DIR}/kura.h)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

include(Antlr4)

set(LLVM_LINK_LLVM_DYLIB ON) 
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

llvm_map_components_to_libnames(llvm_libs 
  x86 
  asmparser 
  support
  core
  analysis
  native
  orcjit
  passes
)

# Clang reads the C headers a program `use`s. Its resource directory holds the
# compiler's own headers (stddef.h, stdint.h): override it at run time with
# KURA_CLANG_RESOURCE_DIR.
find_package(Clang REQUIRED CONFIG)
message(STATUS "Found Clang ${CLANG_PACKAGE_VERSION}")
if(NOT KURA_CLANG_RESOURCE_DIR)
  foreach(candidate
      "${LLVM_LIBRARY_DIR}/clang/${LLVM_VERSION_MAJOR}"
      "${LLVM_LIBRARY_DIR}/clang/${LLVM_VERSION_MAJOR}.${LLVM_VERSION_MINOR}.${LLVM_VERSION_PATCH}")
    if(IS_DIRECTORY "${candidate}/include")
      set(KURA_CLANG_RESOURCE_DIR "${candidate}")
      break()
    endif()
  endforeach()
endif()
message(STATUS "Clang resource dir: ${KURA_CLANG_RESOURCE_DIR}")
if(NOT KURA_CLANG_EXECUTABLE)
  find_program(KURA_CLANG_EXECUTABLE clang HINTS "${LLVM_TOOLS_BINARY_DIR}" NO_DEFAULT_PATH)
  if(NOT KURA_CLANG_EXECUTABLE)
    find_program(KURA_CLANG_EXECUTABLE clang)
  endif()
endif()
message(STATUS "C compiler for imports and shims: ${KURA_CLANG_EXECUTABLE}")

find_package(glm CONFIG REQUIRED)
find_package(yoga CONFIG REQUIRED)
pkg_check_modules(XXHASH REQUIRED IMPORTED_TARGET libxxhash)

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
  element.cc
  element_compiler.cc
  element_printer.cc
  function.cc
  layout.cc
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

target_compile_definitions(kura-runtime
  PUBLIC ${LLVM_DEFINITIONS}
         KURA_CLANG_EXECUTABLE="${KURA_CLANG_EXECUTABLE}"
  PRIVATE KURA_CLANG_RESOURCE_DIR="${KURA_CLANG_RESOURCE_DIR}"
)
target_include_directories(kura-runtime
  PUBLIC ${LLVM_INCLUDE_DIRS}
         ${CLANG_INCLUDE_DIRS}
)
target_link_libraries(kura-runtime
  PUBLIC LLVM
         clang-cpp
         PkgConfig::XXHASH
         yogacore
         antlr::antlr4
         glm::glm
)

option(KURA_BUILD_DEBUGGER "Build kura-debugger (needs GTK4, EGL and epoxy)" ON)

add_subdirectory(compiler/)
if(KURA_BUILD_DEBUGGER)
  add_subdirectory(debugger/)
endif()
add_subdirectory(renderer/)
add_subdirectory(frontend/)
add_subdirectory(backend/)
