add_library(kura-heap STATIC
  arena.cc
  base.cc
  free_list.cc
  heap.cc
  objects.cc
  page_provider.cc
  semispace.cc
  tenured_space.cc)
target_include_directories(kura-heap
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_target_properties(kura-heap PROPERTIES
  POSITION_INDEPENDENT_CODE ON)

add_library(kura-heap-abi-check OBJECT
  abi_check.cc)
target_include_directories(kura-heap-abi-check
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_dependencies(kura-heap kura-heap-abi-check)

target_link_libraries(kura-compiler
  PUBLIC kura-heap)
