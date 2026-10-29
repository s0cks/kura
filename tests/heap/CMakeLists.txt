# Unit tests for the heap, written with GTest.
#
# They need only kura-heap (and GTest): no LLVM, no renderer. Run them with ctest, or run kura-heap-tests directly
# (it takes the usual --gtest_filter and --gtest_list_tests).
find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(kura-heap-tests
  test_arena.cc
  test_header.cc
  test_heap.cc
  test_objects.cc
  test_spaces.cc)
target_link_libraries(kura-heap-tests
  PRIVATE kura-heap
          GTest::gtest_main)
gtest_discover_tests(kura-heap-tests
  DISCOVERY_MODE PRE_TEST)
