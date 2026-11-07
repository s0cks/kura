find_package(jansson CONFIG REQUIRED)
find_package(libuv CONFIG REQUIRED)

add_executable(kura-lsp main.cc
  code_action.cc
  document.cc
  error.cc
  hover.h
  types.cc
  symbol.cc
  completion_item.cc)
target_link_libraries(kura-lsp
  PUBLIC jansson::jansson
         $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
         kurart
         kura-compiler)

add_subdirectory(server)
add_subdirectory(task)
