add_executable(kuralint
  main.cc
  linter.cc)
target_include_directories(kuralint
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(kuralint
  PUBLIC kura-compiler)

add_subdirectory(rules)
