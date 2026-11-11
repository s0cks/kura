find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(kura-text-tests
  test_text.cc)
target_link_libraries(kura-text-tests
  PRIVATE kura-text
          GTest::gtest_main)
gtest_discover_tests(kura-text-tests
  DISCOVERY_MODE PRE_TEST)
