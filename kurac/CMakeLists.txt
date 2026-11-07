add_executable(kurac
  main.cc)
target_link_libraries(kurac
  PUBLIC kura-compiler)

if(TARGET kura-renderer-headless)
  target_link_libraries(kurac
    PRIVATE kura-renderer-headless)
  target_compile_definitions(kurac
    PRIVATE KURA_HAS_HEADLESS_RENDERER)
endif()
