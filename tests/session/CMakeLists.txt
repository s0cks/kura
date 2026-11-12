find_package(GTest REQUIRED)
include(GoogleTest)

# Messages between a UI and its host: a Program, run with the headless renderer
add_executable(kura-session-tests
  test_session.cc)
target_compile_definitions(kura-session-tests
  PRIVATE KURA_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples")
target_link_libraries(kura-session-tests
  PRIVATE kura-compiler
          kura-renderer-headless
          GTest::gtest_main)
gtest_discover_tests(kura-session-tests
  DISCOVERY_MODE PRE_TEST)
