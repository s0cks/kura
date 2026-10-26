# Unit tests for the renderer ABI and the headless renderer, written with GTest.
#
# These need only kura-renderer-headless (and GTest): no LLVM, no GPU. Run them
# with ctest, or run kura-renderer-tests directly (it takes the usual
# --gtest_filter and --gtest_list_tests).
find_package(GTest REQUIRED)
include(GoogleTest)

# The tools the tests share: printers, a Drawer that draws through the ABI,
# golden text files, and the renderer ABI conformance suite.
#
# Another renderer proves it follows the ABI by linking kura-renderer-conformance
# into its own GTest executable and calling kura::test::RegisterConformanceSuite()
# with a factory for itself (see conformance.h).
add_library(kura-test-harness STATIC
  golden.cc)
target_link_libraries(kura-test-harness
  PUBLIC GTest::gtest
         kura-renderer-headless)
target_include_directories(kura-test-harness
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(kura-test-harness
  PRIVATE KURA_TEST_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

add_library(kura-renderer-conformance STATIC
  conformance.cc)
target_link_libraries(kura-renderer-conformance
  PUBLIC kura-test-harness)

add_executable(kura-renderer-tests
  test_conformance_headless.cc
  test_headless.cc
  test_headless_golden.cc)
target_link_libraries(kura-renderer-tests
  PRIVATE kura-renderer-conformance
          GTest::gtest_main)
gtest_discover_tests(kura-renderer-tests
  DISCOVERY_MODE PRE_TEST)

# PNG / JPEG output and golden *image* tests: only when kura-renderer-image is
# built (it needs libpng and libjpeg).
if(TARGET kura-renderer-image)
  add_library(kura-test-harness-image STATIC
    golden_image.cc)
  target_link_libraries(kura-test-harness-image
    PUBLIC kura-test-harness
           kura-renderer-image)
  target_compile_definitions(kura-test-harness-image
    PRIVATE KURA_TEST_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
            KURA_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}")

  add_executable(kura-renderer-image-tests
    test_image.cc)
  target_link_libraries(kura-renderer-image-tests
    PRIVATE kura-test-harness-image
            GTest::gtest_main)
  gtest_discover_tests(kura-renderer-image-tests
    DISCOVERY_MODE PRE_TEST)
endif()
