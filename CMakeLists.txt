cmake_minimum_required(VERSION 3.29)
project(kura
  VERSION 0.0.1
  HOMEPAGE_URL https://github.com/s0cks/kura
  LANGUAGES CXX C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(CMAKE_BUILD_TYPE MATCHES "^[Dd]ebug")
  add_compile_definitions(KURA_DEBUG)
  add_compile_definitions(_NDEBUG)
endif()

find_package(PkgConfig REQUIRED)

find_package(glm CONFIG REQUIRED)
pkg_check_modules(XXHASH REQUIRED IMPORTED_TARGET libxxhash)
include(clang)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

include_directories("${CMAKE_CURRENT_SOURCE_DIR}")
add_subdirectory(heap)
add_subdirectory(runtime)
add_subdirectory(layout)
add_subdirectory(compiler)

add_subdirectory(renderer)

add_subdirectory(kurac)

option(KURA_BUILD_DEBUGGER "Build kura-debugger (needs GTK4, EGL and epoxy)" OFF)
if(KURA_BUILD_DEBUGGER)
  add_subdirectory(debugger)
endif()

option(KURA_BUILD_LSP "Build lsp (needs libuv)" ON)
if(KURA_BUILD_LSP)
  add_subdirectory(lsp)
endif()

option(KURA_BUILD_FORMATTER "Build kurafmt" ON)
if(KURA_BUILD_FORMATTER)
  add_subdirectory(kurafmt)
endif()

option(KURA_BUILD_TESTS "Build the unit tests (run them with ctest)" ON)
if(KURA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
