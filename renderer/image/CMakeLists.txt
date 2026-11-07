message(STATUS "building the image renderer")

find_package(PNG REQUIRED)
find_package(JPEG REQUIRED)

add_library(kura-renderer-image STATIC
  renderer.cc)
target_link_libraries(kura-renderer-image
  PUBLIC kura-renderer-base
         PNG::PNG
         JPEG::JPEG)
