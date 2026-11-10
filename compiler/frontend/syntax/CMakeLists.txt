add_library(kura-syntax STATIC
  ast.cc
  ast_printer.cc
  diagnostic.cc
  lexer.cc
  parser.cc)
target_link_libraries(kura-syntax
  PUBLIC kura-heap)
target_include_directories(kura-syntax
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_compile_features(kura-syntax
  PUBLIC cxx_std_23)
set_target_properties(kura-syntax PROPERTIES
  POSITION_INDEPENDENT_CODE ON)
