# Text: HarfBuzz shaping, FreeType rasterization, and the lookup of a font family on the system. It depends on
# nothing else in Kura, so every renderer (and layout's tests) can share it.
find_package(Freetype REQUIRED)
pkg_check_modules(HARFBUZZ REQUIRED IMPORTED_TARGET harfbuzz)
pkg_check_modules(FONTCONFIG IMPORTED_TARGET fontconfig)

add_library(kura-text STATIC text.cc)
set_target_properties(kura-text PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(kura-text PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(kura-text
  PUBLIC Freetype::Freetype
         PkgConfig::HARFBUZZ)
if(FONTCONFIG_FOUND)
  message(STATUS "kura-text: finding system fonts with fontconfig")
  target_link_libraries(kura-text PRIVATE PkgConfig::FONTCONFIG)
  target_compile_definitions(kura-text PRIVATE KURA_TEXT_HAS_FONTCONFIG=1)
else()
  message(STATUS "kura-text: no fontconfig, falling back to well-known font paths")
endif()
