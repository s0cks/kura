set(KURA_MAX_VERTICES "16384" CACHE STRING "The max number of vertices the renderer should have")
set(KURA_MAX_INDICES "32768" CACHE STRING "The max number of indices the renderer should have")

add_library(kura-renderer-base INTERFACE)
target_include_directories(kura-renderer-base
  INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_target_properties(kura-renderer-base PROPERTIES
  POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(kura-renderer-base
  INTERFACE
    KURA_MAX_NUMBER_OF_VERTICES=${KURA_MAX_VERTICES}
    KURA_MAX_NUMBER_OF_INDICES=${KURA_MAX_INDICES}
)

option(KURA_BUILD_HEADLESS_RENDERER "Build the headless renderer" ON)
if(KURA_BUILD_HEADLESS_RENDERER)
  message(STATUS "building the headless renderer")
  add_library(kura-renderer-headless STATIC
    renderer_headless.cc)
  target_link_libraries(kura-renderer-headless
    PUBLIC
      kura-renderer-base)
endif()

option(KURA_BUILD_IMAGE_RENDERER "Build kura-renderer-image (PNG/JPEG output; needs libpng and libjpeg)" ON)
if(KURA_BUILD_IMAGE_RENDERER)
  message(STATUS "building the image renderer")
  find_package(PNG REQUIRED)
  find_package(JPEG REQUIRED)
  add_library(kura-renderer-image STATIC
    renderer_image.cc)
  target_link_libraries(kura-renderer-image
    PUBLIC kura-renderer-base
           PNG::PNG
           JPEG::JPEG)
endif()

option(KURA_BUILD_GL_RENDERER "Build the gl renderer" OFF)
if(KURA_BUILD_GL_RENDERER)
  message(STATUS "building the OpenGL renderer")
  pkg_check_modules(EGL   REQUIRED IMPORTED_TARGET egl)
  pkg_check_modules(EPOXY REQUIRED IMPORTED_TARGET epoxy)

  add_library(kura-renderer-gl SHARED
    renderer_gl.cc)
  target_compile_definitions(kura-renderer-gl
    PUBLIC 
      KURA_RENDER_TARGET_OPENGL)
  target_link_libraries(kura-renderer-gl
    PUBLIC kura-renderer-base
           PkgConfig::EGL
           PkgConfig::EPOXY)
endif()

option(KURA_BUILD_VK_RENDERER "Build the vulkan renderer" OFF)
if(KURA_BUILD_VK_RENDERER)
  message(STATUS "building the Vulkan renderer")
endif()
