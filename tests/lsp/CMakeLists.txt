find_package(GTest REQUIRED)
find_package(jansson CONFIG REQUIRED)
include(GoogleTest)

# The server itself is an executable, so the parts that don't need libuv (completions, code
# actions, documents) are compiled straight into the test binary.
add_executable(kura-lsp-tests
  test_lsp.cc)
target_link_libraries(kura-lsp-tests
  PRIVATE jansson::jansson
          kurart
          kura-syntax
          kura-lsp-common
          GTest::gtest_main)
gtest_discover_tests(kura-lsp-tests
  DISCOVERY_MODE PRE_TEST)
