cmake_minimum_required(VERSION 3.29)
project(kura
  VERSION 0.0.1
  HOMEPAGE_URL https://github.com/s0cks/kura
  LANGUAGES CXX C)

if(CMAKE_BUILD_TYPE MATCHES "^[Dd]ebug")
  add_compile_definitions(KURA_DEBUG)
  add_compile_definitions(_NDEBUG)
endif()

find_package(PkgConfig REQUIRED)

option(KURA_BUILD_DEBUGGER "Build kura-debugger (needs GTK4, EGL and epoxy)" OFF)
option(KURA_BUILD_TESTS "Build the unit tests (run them with ctest)" ON)
if(KURA_BUILD_TESTS)
  enable_testing()
endif()

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
pkg_check_modules(XXHASH REQUIRED IMPORTED_TARGET libxxhash)

add_subdirectory(Sources/)

if(KURA_BUILD_TESTS)
  add_subdirectory(tests/renderer)
  add_subdirectory(tests/dom)
endif()
