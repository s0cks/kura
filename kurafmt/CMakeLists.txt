add_executable(kurafmt
  main.cc
  document.cc
  document_builder.cc
  document_render.cc)
target_include_directories(kurafmt
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(kurafmt
  PUBLIC kura-compiler)
