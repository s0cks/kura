find_package(jansson CONFIG REQUIRED)
find_package(libuv CONFIG REQUIRED)

add_library(kura-lsp-common STATIC
  code_action.cc
  document.cc
  error.cc
  hover.h
  keyword_completion.cc
  types.cc
  symbol.cc
  diagnostic.cc
  completion_item.cc)
target_link_libraries(kura-lsp-common
  PUBLIC jansson::jansson
         $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
         kurart
         kura-compiler)

add_executable(kura-lsp main.cc)
target_link_libraries(kura-lsp
  PUBLIC kura-lsp-common)

add_subdirectory(server)
add_subdirectory(task)
