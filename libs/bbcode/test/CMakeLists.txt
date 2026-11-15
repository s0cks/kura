find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(bbcode-parser-tests
  parser.cc)
target_link_libraries(bbcode-parser-tests
  PUBLIC bbcode
         GTest::gtest_main)
gtest_discover_tests(bbcode-parser-tests
  DISCOVERY_MODE PRE_TEST)

