#include <cstdlib>
#include <print>

#include "log.h"
#include "lsp/server.h"

using namespace kura;

auto main(int argc, char** argv) -> int {
  log::Init("/home/tazz/.local/state/kura-lsp.log");
  log::info("starting lsp...");
  lsp::Server server(uv_default_loop());
  server.Start();
  return EXIT_SUCCESS;
}
