add_library(djot STATIC)
target_include_directories(djot
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_subdirectory(src)

add_subdirectory(tests)

add_subdirectory(benchmarks)
