find_package(jansson CONFIG REQUIRED)
find_package(argparse CONFIG REQUIRED)

pkg_check_modules(EDITORCONFIG QUIET IMPORTED_TARGET editorconfig)

add_executable(kurac
  main.cc)
target_include_directories(kurac
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(kurac
  PUBLIC kura-compiler
         kura-explain
         argparse::argparse
         jansson::jansson)

if(EDITORCONFIG_FOUND)
  message(STATUS "building with editorconfig support")
  target_compile_definitions(kurac
    PUBLIC KURA_HAS_EDITORCONFIG)
  target_link_libraries(kurac
    PRIVATE PkgConfig::EDITORCONFIG)
  target_sources(kurac
    PRIVATE editorconfig_wrapper.cc)
else()
  message(STATUS "editorconfig not found")
endif()

if(TARGET kura-renderer-headless)
  target_link_libraries(kurac
    PRIVATE kura-renderer-headless)
  target_compile_definitions(kurac
    PRIVATE KURA_HAS_HEADLESS_RENDERER)
endif()
