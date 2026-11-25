find_package(zstd CONFIG REQUIRED)

set(Python3_FIND_VIRTUALENV FIRST)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
file(GLOB DJOT_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.dj")
set(PACKED_DOCS_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/packed_docs.cc")

add_custom_command(
  OUTPUT "${PACKED_DOCS_SOURCE}"
  COMMAND "${Python3_EXECUTABLE}"
          "${CMAKE_SOURCE_DIR}/scripts/pack_docs.py"
          "${CMAKE_CURRENT_SOURCE_DIR}"
          "${PACKED_DOCS_SOURCE}"
  DEPENDS ${DJOT_FILES} "${CMAKE_SOURCE_DIR}/scripts/pack_docs.py"
  COMMENT "Packing and Zstd-compressing Djot error explanations"
  VERBATIM
)
add_custom_target(kurac_pack_docs
  DEPENDS "${PACKED_DOCS_SOURCE}")

target_sources(kurac
  PRIVATE "${PACKED_DOCS_SOURCE}"
          index.cc
          renderer.cc
          suggest.cc)
target_link_libraries(kurac
  PRIVATE zstd::libzstd
          djot)
add_dependencies(kurac kurac_pack_docs)
