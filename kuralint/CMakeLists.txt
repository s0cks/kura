find_package(jansson CONFIG REQUIRED)
find_package(argparse CONFIG REQUIRED)

add_executable(kuralint
  main.cc
  linter.cc)
target_include_directories(kuralint
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(kuralint
  PUBLIC kura-compiler
         argparse::argparse
         jansson::jansson)

add_subdirectory(rules)
