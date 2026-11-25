find_package(jansson CONFIG REQUIRED)
find_package(argparse CONFIG REQUIRED)

add_executable(kurac
  main.cc)
target_link_libraries(kurac
  PUBLIC kura-compiler
         argparse::argparse
         jansson::jansson)

if(TARGET kura-renderer-headless)
  target_link_libraries(kurac
    PRIVATE kura-renderer-headless)
  target_compile_definitions(kurac
    PRIVATE KURA_HAS_HEADLESS_RENDERER)
endif()
