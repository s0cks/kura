find_package(GTest REQUIRED)
include(GoogleTest)

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

add_executable(kura-runtime-renderer-tests
  test_runtime_renderer.cc)
target_link_libraries(kura-runtime-renderer-tests
  PRIVATE kura-compiler
          kura-renderer-headless
          GTest::gtest_main)
gtest_discover_tests(kura-runtime-renderer-tests
  DISCOVERY_MODE PRE_TEST)

if(TARGET kura-renderer-gl)
  pkg_check_modules(EGL REQUIRED IMPORTED_TARGET egl)
  add_executable(kura-renderer-gl-tests
    test_gl.cc)
  target_link_libraries(kura-renderer-gl-tests
    PRIVATE kura-renderer-conformance
            kura-renderer-gl
            PkgConfig::EGL
            GTest::gtest_main)
  gtest_discover_tests(kura-renderer-gl-tests
    DISCOVERY_MODE PRE_TEST)

  # a Kura program, from source to pixels
  add_executable(kura-gl-program-tests
    test_gl_program.cc)
  target_compile_definitions(kura-gl-program-tests
    PRIVATE KURA_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples")
  target_link_libraries(kura-gl-program-tests
    PRIVATE kura-compiler
            kura-renderer-gl
            PkgConfig::EGL
            GTest::gtest_main)
  gtest_discover_tests(kura-gl-program-tests
    DISCOVERY_MODE PRE_TEST)
endif()
