message(STATUS "building the OpenGL renderer")
pkg_check_modules(EPOXY REQUIRED IMPORTED_TARGET epoxy)

add_library(kura-renderer-gl STATIC
  renderer.cc)
target_link_libraries(kura-renderer-gl
  PUBLIC kura-renderer-base
         kura-text
         PkgConfig::EPOXY
         kura-tracing)

option(KURA_BUILD_GL_DEMO "Build kura-gl-demo, which draws a rect in a loop through the runtime (needs GLFW)" ON)
if(KURA_BUILD_GL_DEMO)
  pkg_check_modules(GLFW REQUIRED IMPORTED_TARGET glfw3)
  message(STATUS "building the gl demo")
  add_executable(kura-gl-demo demo.cc)
  target_link_libraries(kura-gl-demo
    PRIVATE kura-renderer-gl
            kura-compiler
            PkgConfig::GLFW)
endif()

option(KURA_BUILD_GL_BENCHMARKS "Build the benchmarks for the gl renderer" ON)
if(KURA_BUILD_GL_BENCHMARKS)
  message(STATUS "building gl renderer benchmarks")
  add_subdirectory(benchmark)
endif()
