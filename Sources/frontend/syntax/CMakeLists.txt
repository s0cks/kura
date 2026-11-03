# The syntax layer: source files, tokens, the lexer, the syntax tree, the parser and diagnostics.
#
# It depends on the standard library and nothing else, on purpose. That keeps it cheap to unit test
# and to fuzz, and lets the LSP link it (through CGo) without LLVM, Clang or the Kura heap. Anything
# that needs to know what a name *means* belongs in the ExprBuilder (frontend/expr_builder.cc), which
# walks the tree this library produces.
add_library(kura-syntax STATIC
  ast.cc
  ast_printer.cc
  diagnostic.cc
  lexer.cc
  parser.cc)
target_include_directories(kura-syntax
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_compile_features(kura-syntax
  PUBLIC cxx_std_23)
set_target_properties(kura-syntax PROPERTIES
  POSITION_INDEPENDENT_CODE ON)
