option(KURA_RENDER_TARGET "The render target for kura. [OPENGL/VULKAN]" "OPENGL")

option(KURA_MAX_VERTICES "The max number of vertices the renderer should have" 16384)
option(KURA_MAX_INDICES "The max number of indices the renderer should have" 32768)

target_compile_definitions(kura-runtime
  PUBLIC
    KURA_MAX_NUMBER_OF_VERTICES=${KURA_MAX_VERTICES}
    KURA_MAX_NUMBER_OF_INDICES=${KURA_MAX_INDICES}
)

target_sources(kura-runtime
  PUBLIC renderer.cc
         renderer_gl.cc
         renderer_vk.cc)

if(${KURA_RENDER_TARGET} STREQUAL "OPENGL")
  target_compile_definitions(kura-runtime
    PUBLIC 
      KURA_RENDER_TARGET_OPENGL)
elseif(${KURA_RENDER_TARGET} STREQUAL "VULKAN")
  target_compile_definitions(kura-runtime
    PUBLIC
      KURA_RENDER_TARGET_VULKAN)
endif()
