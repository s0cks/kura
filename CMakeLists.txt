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

# The major version of LLVM to build against. Empty builds against whichever LLVM find_package finds
# first; a preset (clang-llvm20-debug) or -DKURA_LLVM_VERSION=20 pins one. Kura builds with LLVM 20
# and LLVM 22: the few places that differ are guarded on LLVM_VERSION_MAJOR. If the LLVM you want
# isn't in one of the places looked in below, point -DLLVM_DIR and -DClang_DIR at its lib/cmake.
set(KURA_LLVM_VERSION "" CACHE STRING "Major version of LLVM to build against (empty: any)")
set(KURA_LLVM_HINTS "")
if(KURA_LLVM_VERSION)
  list(APPEND KURA_LLVM_HINTS
    "/usr/lib/llvm-${KURA_LLVM_VERSION}"              # Debian, Ubuntu (apt.llvm.org)
    "/opt/homebrew/opt/llvm@${KURA_LLVM_VERSION}"     # Homebrew, Apple silicon
    "/usr/local/opt/llvm@${KURA_LLVM_VERSION}")       # Homebrew, Intel
endif()

set(LLVM_LINK_LLVM_DYLIB ON) 
# (not find_package(LLVM <version>): LLVM's version file only accepts a matching major.minor)
find_package(LLVM REQUIRED CONFIG HINTS ${KURA_LLVM_HINTS})
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
if(KURA_LLVM_VERSION AND NOT LLVM_VERSION_MAJOR EQUAL KURA_LLVM_VERSION)
  message(FATAL_ERROR
    "KURA_LLVM_VERSION is ${KURA_LLVM_VERSION}, but the LLVM found is ${LLVM_PACKAGE_VERSION} "
    "(${LLVM_DIR}). Point -DLLVM_DIR and -DClang_DIR at an LLVM ${KURA_LLVM_VERSION}, or clear "
    "LLVM_DIR from the cache if it's left over from another build.")
endif()
if(LLVM_VERSION_MAJOR LESS 20)
  message(FATAL_ERROR "Kura needs LLVM 20 or newer, found ${LLVM_PACKAGE_VERSION}")
endif()

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

find_package(Clang REQUIRED CONFIG HINTS ${KURA_LLVM_HINTS})
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
  add_subdirectory(tests)
endif()
