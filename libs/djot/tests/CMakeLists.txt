find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(djot-parser-tests
  test_parser.cc)
target_link_libraries(djot-parser-tests
  PRIVATE djot
          GTest::gtest_main)
gtest_discover_tests(djot-parser-tests
  DISCOVERY_MODE PRE_TEST)
