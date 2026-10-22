set(KURA_RENDER_TARGET "OpenGL" CACHE STRING "The backend graphics API to target")
string(TOUPPER "${KURA_RENDER_TARGET}" KURA_RENDER_TARGET_UPPER)

set(KURA_MAX_VERTICES "16384" CACHE STRING "The max number of vertices the renderer should have")
set(KURA_MAX_INDICES "32768" CACHE STRING "The max number of indices the renderer should have")

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
endif()
