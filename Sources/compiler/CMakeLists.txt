add_executable(kura
  main.cc)
target_link_libraries(kura
  PUBLIC kura-compiler)
# `--run` draws into the headless renderer, when it's built
if(TARGET kura-renderer-headless)
  target_link_libraries(kura
    PRIVATE kura-renderer-headless)
  target_compile_definitions(kura
    PRIVATE KURA_HAS_HEADLESS_RENDERER)
endif()
