set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib)

add_subdirectory(renderer)
add_subdirectory(dom)
add_subdirectory(heap)
add_subdirectory(layout)
add_subdirectory(syntax)

option(ENABLE_FUZZING "Enable fuzzing" OFF)
if(ENABLE_FUZZING)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    message(FATAL_ERROR "Fuzzing requires Clang or GCC for sanitizer")
  endif()

  add_subdirectory(fuzzing)
endif()
