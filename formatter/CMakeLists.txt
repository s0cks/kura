pkg_check_modules(YAML REQUIRED IMPORTED_TARGET yaml-0.1)

target_sources(kura-compiler
  PRIVATE config.cc
          config_json.cc
          config_yaml.cc
          document.cc
          document_builder.cc
          document_render.cc
          formatter.cc)
target_link_libraries(kura-compiler
  PRIVATE PkgConfig::YAML)

if(PkgConfig::EDITORCONFIG)
  target_link_libraries(kura-compiler
    PRIVATE PkgConfig::EDITORCONFIG)
  target_compile_definitions(kura-compiler KURA_HAS_EDITORCONFIG)
endif()
