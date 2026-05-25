list(APPEND ANTLR_ARGS
  -Dlanguage=Cpp
  -visitor
  -o "${CMAKE_CURRENT_BINARY_DIR}"
)

find_program(ANTLR NAMES "antlr4")
if(ANTLR)
  execute_process(
    COMMAND ${ANTLR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE ANTLR_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  string(REGEX MATCH "[0-9]+\.[0-9]+\.[0-9]+" ANTLR_VERSION "${ANTLR_VERSION}")

  message(STATUS "found antlr4 v${ANTLR_VERSION}: ${ANTLR}")

  add_library(antlr4 INTERFACE)
  target_include_directories(antlr4
    INTERFACE /usr/include/antlr4-runtime/)
  target_link_libraries(antlr4
    INTERFACE antlr4-runtime)
  add_library(antlr::antlr4 ALIAS antlr4)
endif()
