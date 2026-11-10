#include <cstdlib>
#include <print>

#include "common.h"
#include "heap/heap.h"
#include "log.h"
#include "lsp/server/server.h"

auto main(int argc, char** argv) -> int {
  using namespace kura;

  log::Init("/home/tazz/.local/state/kura-lsp.log");
  log::info("starting lsp...");

  const auto heap = new heap::Heap(heap::Heap::SmallOptions());
  KASSERT(heap);
  heap::Heap::SetCurrent(heap);

  lsp::Server server(uv_default_loop());
  server.Start();
  return EXIT_SUCCESS;
}
