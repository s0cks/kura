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

add_subdirectory(Sources/)

# # Now build our tools
# add_executable(simple-tool tool.cpp)
#
# # Find the libraries that correspond to the LLVM components
# # that we wish to use
# llvm_map_components_to_libnames(llvm_libs support core irreader)
#
# # Link against LLVM libraries
# target_link_libraries(simple-tool ${llvm_libs})
