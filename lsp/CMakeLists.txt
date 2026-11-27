find_package(jansson CONFIG REQUIRED)
find_package(libuv CONFIG REQUIRED)
find_package(argparse CONFIG REQUIRED)

add_library(kura-lsp-common STATIC
  binder.cc
  code_action.cc
  completion.cc
  completion_item.cc
  describe.cc
  diagnostic.cc
  document.cc
  error.cc
  formatting.cc
  hover.cc
  inlay_hints.cc
  keyword_completion.cc
  modules.cc
  navigation.cc
  outline.cc
  semantic_tokens.cc
  signature_help.cc
  symbol.cc
  types.cc
  uri.cc)
target_link_libraries(kura-lsp-common
  PUBLIC jansson::jansson
         $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
         kurart
         kura-compiler)

add_executable(kura-lsp main.cc)
target_link_libraries(kura-lsp
  PUBLIC kura-lsp-common
         argparse::argparse)

add_subdirectory(server)
add_subdirectory(task)
