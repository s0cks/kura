find_package(zstd CONFIG REQUIRED)

set(Python3_FIND_VIRTUALENV FIRST)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
file(GLOB DJOT_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/data/*.dj")
set(PACKED_DOCS_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/packed_docs.cc")

add_custom_command(
  OUTPUT "${PACKED_DOCS_SOURCE}"
  COMMAND "${Python3_EXECUTABLE}"
          "${CMAKE_SOURCE_DIR}/scripts/pack_docs.py"
          "-i"
          "${CMAKE_CURRENT_SOURCE_DIR}/data"
          "-o"
          "${PACKED_DOCS_SOURCE}"
  DEPENDS ${DJOT_FILES} "${CMAKE_SOURCE_DIR}/scripts/pack_docs.py"
  COMMENT "Packing and Zstd-compressing Djot error explanations"
  VERBATIM
)
add_custom_target(kurac_pack_docs
  DEPENDS "${PACKED_DOCS_SOURCE}")

add_library(kura-explain STATIC
  "${PACKED_DOCS_SOURCE}"
  index.cc
  renderer.cc
  suggest.cc)
target_link_libraries(kura-explain
  PUBLIC zstd::libzstd
         djot
         rang::rang)
add_dependencies(kura-explain kurac_pack_docs)
