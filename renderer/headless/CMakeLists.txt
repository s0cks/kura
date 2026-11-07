message(STATUS "building the headless renderer")
add_library(kura-renderer-headless STATIC
  renderer.cc)
target_link_libraries(kura-renderer-headless
  PUBLIC kura-renderer-base)
