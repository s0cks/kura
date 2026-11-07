find_package(jansson CONFIG REQUIRED)
find_package(libuv CONFIG REQUIRED)

add_executable(kura-lsp
  main.cc
  types.cc
  server.cc
  request.cc
  response.cc
  router.cc
  error.cc
  params.cc
  document.cc)
target_link_libraries(kura-lsp
  PUBLIC jansson::jansson
         $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
         kurart
         kura-compiler)

add_subdirectory(task)
