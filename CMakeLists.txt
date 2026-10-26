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

option(KURA_BUILD_TESTS "Build the unit tests (run them with ctest)" ON)
if(KURA_BUILD_TESTS)
  enable_testing()
endif()

add_subdirectory(Sources/)

if(KURA_BUILD_TESTS)
  add_subdirectory(tests/renderer)
endif()
