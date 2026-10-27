find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(kura-dom-tests
  test_main.cc
  test_property.cc
  test_builder.cc
  test_lowering.cc)
target_link_libraries(kura-dom-tests
  PRIVATE kura-compiler
          GTest::gtest
          GTest::gtest_main)
gtest_discover_tests(kura-dom-tests
  DISCOVERY_MODE PRE_TEST)
