find_package(jansson CONFIG REQUIRED)
find_package(argparse CONFIG REQUIRED)

add_executable(kurac
  main.cc)
target_include_directories(kurac
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(kurac
  PUBLIC kura-compiler
         kura-explain
         argparse::argparse
         jansson::jansson)

if(TARGET kura-renderer-headless)
  target_link_libraries(kurac
    PRIVATE kura-renderer-headless)
  target_compile_definitions(kurac
    PRIVATE KURA_HAS_HEADLESS_RENDERER)
endif()
