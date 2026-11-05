find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(kura-layout-tests test_layout.cc)
target_link_libraries(kura-layout-tests
  PRIVATE kura-layout
          GTest::gtest
          GTest::gtest_main)
gtest_discover_tests(kura-layout-tests
  DISCOVERY_MODE PRE_TEST)

# The compiler end to end: programs are compiled, run on the headless renderer, and the rects they fill are
# compared with what is worked out by hand in programs/*.expected.
if(TARGET kura)
  add_test(NAME kura-layout-programs
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run.sh $<TARGET_FILE:kura>)
endif()
