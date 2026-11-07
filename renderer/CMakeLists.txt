set(KURA_MAX_VERTICES "16384" CACHE STRING "The max number of vertices the renderer should have")
set(KURA_MAX_INDICES "32768" CACHE STRING "The max number of indices the renderer should have")

add_library(kura-renderer-base INTERFACE)
set_target_properties(kura-renderer-base PROPERTIES
  POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(kura-renderer-base
  INTERFACE
    KURA_MAX_NUMBER_OF_VERTICES=${KURA_MAX_VERTICES}
    KURA_MAX_NUMBER_OF_INDICES=${KURA_MAX_INDICES}
)

option(KURA_BUILD_HEADLESS_RENDERER "Build the headless renderer" ON)
if(KURA_BUILD_HEADLESS_RENDERER)
  add_subdirectory(headless)
endif()

option(KURA_BUILD_IMAGE_RENDERER "Build kura-renderer-image (PNG/JPEG output; needs libpng and libjpeg)" ON)
if(KURA_BUILD_IMAGE_RENDERER)
  add_subdirectory(image)
endif()

option(KURA_BUILD_GL_RENDERER "Build the OpenGL renderer (needs libepoxy)" ON)
if(KURA_BUILD_GL_RENDERER)
  add_subdirectory(gl)
endif()

option(KURA_BUILD_VK_RENDERER "Build the vulkan renderer" OFF)
if(KURA_BUILD_VK_RENDERER)
  add_subdirectory(vulkan)
endif()
