# Unit tests for the geometry types (runtime/geometry.h), written with GTest. The header has no dependencies, so
# neither do they: only GTest.
find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(kura-geometry-tests
  test_geometry.cc)
target_include_directories(kura-geometry-tests
  PRIVATE ${PROJECT_SOURCE_DIR}/runtime
          ${PROJECT_SOURCE_DIR})
target_link_libraries(kura-geometry-tests
  PRIVATE GTest::gtest_main)
gtest_discover_tests(kura-geometry-tests
  DISCOVERY_MODE PRE_TEST)
