#include <cstdlib>
#include <filesystem>
#include <print>
#include <string>

#include "common.h"
#include "heap/heap.h"
#include "log.h"
#include "lsp/server/server.h"

namespace {
// $KURA_LSP_LOG, else $XDG_STATE_HOME/kura/lsp.log, else ~/.local/state/kura/lsp.log.
auto GetLogPath() -> std::string {
  namespace fs = std::filesystem;
  if (const auto* path = std::getenv("KURA_LSP_LOG"); path && *path)
    return path;

  fs::path dir{};
  if (const auto* state = std::getenv("XDG_STATE_HOME"); state && *state)
    dir = fs::path(state);
  else if (const auto* home = std::getenv("HOME"); home && *home)
    dir = fs::path(home) / ".local" / "state";
  else
    dir = fs::temp_directory_path();

  dir /= "kura";
  std::error_code ec{};
  fs::create_directories(dir, ec);
  return (dir / "lsp.log").string();
}
}  // namespace

auto main(int argc, char** argv) -> int {
  using namespace kura;

  log::Init(GetLogPath());
  log::info("starting lsp...");

  const auto heap = new heap::Heap(heap::Heap::SmallOptions());
  KASSERT(heap);
  heap::Heap::SetCurrent(heap);

  lsp::Server server(uv_default_loop());
  server.Start();
  return EXIT_SUCCESS;
}
