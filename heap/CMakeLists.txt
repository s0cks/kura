add_library(kura-heap STATIC
  arena.cc
  base.cc
  free_list.cc
  heap.cc
  objects.cc
  page_provider.cc
  semispace.cc
  tenured_space.cc)
set_target_properties(kura-heap PROPERTIES
  POSITION_INDEPENDENT_CODE ON)
