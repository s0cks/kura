# One place that says whether Kura is built with Tracy, so the libraries that are instrumented don't each decide.
# Link it where `tracing/tracing.h` is used: with tracing off it is empty, and with it on it defines
# KURA_TRACING_ENABLED and brings the Tracy client.
#
# Not for kurart or kura-layout: kurart is a shared library, and there has to be one Tracy client in a process.
add_library(kura-tracing INTERFACE)
target_include_directories(kura-tracing INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
if(KURA_ADD_TRACING_SUPPORT)
  target_compile_definitions(kura-tracing INTERFACE KURA_TRACING_ENABLED)
  target_link_libraries(kura-tracing INTERFACE Tracy::TracyClient)
endif()
