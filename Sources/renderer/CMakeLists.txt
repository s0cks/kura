# The graphics API the runtime renders with: OpenGL, Vulkan, or Headless (no
# graphics API at all: nothing is drawn, so it needs no GPU, window or driver).
set(KURA_RENDER_TARGET "OpenGL" CACHE STRING "The backend graphics API to target")
string(TOUPPER "${KURA_RENDER_TARGET}" KURA_RENDER_TARGET_UPPER)

set(KURA_MAX_VERTICES "16384" CACHE STRING "The max number of vertices the renderer should have")
set(KURA_MAX_INDICES "32768" CACHE STRING "The max number of indices the renderer should have")

# The headless renderer implements the renderer ABI (renderer_abi.h) without any
# dependencies, whatever the render target is. The unit tests link it alone, and
# kura-runtime (so kurac) links it too.
add_library(kura-renderer-headless STATIC
  renderer_headless.cc)
target_include_directories(kura-renderer-headless
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_target_properties(kura-renderer-headless PROPERTIES
  POSITION_INDEPENDENT_CODE ON)

# PNG and JPEG output of what the headless renderer drew, for visual tests. Kept
# apart from kura-renderer-headless so that one has no dependencies.
option(KURA_BUILD_IMAGE_RENDERER "Build kura-renderer-image (PNG/JPEG output; needs libpng and libjpeg)" ON)
if(KURA_BUILD_IMAGE_RENDERER)
  find_package(PNG REQUIRED)
  find_package(JPEG REQUIRED)
  add_library(kura-renderer-image STATIC
    renderer_image.cc)
  target_link_libraries(kura-renderer-image
    PUBLIC kura-renderer-headless
           PNG::PNG
           JPEG::JPEG)
  set_target_properties(kura-renderer-image PROPERTIES
    POSITION_INDEPENDENT_CODE ON)
endif()

target_compile_definitions(kura-runtime
  PUBLIC
    KURA_MAX_NUMBER_OF_VERTICES=${KURA_MAX_VERTICES}
    KURA_MAX_NUMBER_OF_INDICES=${KURA_MAX_INDICES}
)

target_sources(kura-runtime
  PUBLIC renderer.cc
         batch_gl.cc
         renderer_gl.cc
         renderer_vk.cc
         vertex.cc
         vertex_gl.cc)
target_link_libraries(kura-runtime
  PUBLIC kura-renderer-headless)

message(STATUS "render target: ${KURA_RENDER_TARGET_UPPER}")
if(${KURA_RENDER_TARGET_UPPER} STREQUAL "OPENGL")
  pkg_check_modules(EGL   REQUIRED IMPORTED_TARGET egl)
  pkg_check_modules(EPOXY REQUIRED IMPORTED_TARGET epoxy)

  target_compile_definitions(kura-runtime
    PUBLIC 
      KURA_RENDER_TARGET_OPENGL)
  target_link_libraries(kura-runtime
    PUBLIC PkgConfig::EGL
           PkgConfig::EPOXY)
elseif(${KURA_RENDER_TARGET_UPPER} STREQUAL "VULKAN")
  target_compile_definitions(kura-runtime
    PUBLIC
      KURA_RENDER_TARGET_VULKAN)
elseif(${KURA_RENDER_TARGET_UPPER} STREQUAL "HEADLESS")
  target_compile_definitions(kura-runtime
    PUBLIC
      KURA_RENDER_TARGET_HEADLESS)
else()
  message(FATAL_ERROR "unknown KURA_RENDER_TARGET '${KURA_RENDER_TARGET}': expected OpenGL, Vulkan or Headless")
endif()
