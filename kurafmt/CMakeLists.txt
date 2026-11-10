find_package(jansson CONFIG REQUIRED)
pkg_check_modules(YAML REQUIRED IMPORTED_TARGET yaml-0.1)

add_executable(kurafmt
  main.cc
  config.cc
  config_json.cc
  config_yaml.cc
  document.cc
  document_builder.cc
  document_render.cc
  formatter.cc)
target_include_directories(kurafmt
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(kurafmt
  PUBLIC kura-compiler
         rang
         PkgConfig::YAML)
