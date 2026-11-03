# Tests for the syntax layer (lexer, parser, diagnostics), written with GTest.
#
# They need only kura-syntax (and GTest): no LLVM, no ANTLR, no heap. Run them with ctest, or run
# kura-syntax-tests directly (it takes the usual --gtest_filter and --gtest_list_tests).
find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(kura-syntax-tests
  test_lexer.cc
  test_parser.cc
  test_recovery.cc
  test_robustness.cc)
target_compile_definitions(kura-syntax-tests
  PRIVATE KURA_GRAMMAR_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../grammar")
target_link_libraries(kura-syntax-tests
  PRIVATE kura-syntax
          GTest::gtest_main)
gtest_discover_tests(kura-syntax-tests
  DISCOVERY_MODE PRE_TEST)

# A small command line front end for poking at the parser by hand, and for tests/grammar/run_tests.py.
add_executable(kura-syntax-dump dump.cc)
target_link_libraries(kura-syntax-dump PRIVATE kura-syntax)
