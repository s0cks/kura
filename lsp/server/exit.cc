#include <cstdio>
#include <cstdlib>

#include "lsp/server/exit.h"

#include "lsp/server/server.h"

namespace kura::lsp {
void Server::HandleExit(const Request& req) {
  log::info("exit");
  // 0 only when `shutdown` came first, as the protocol asks.
  // Not `std::exit`: it runs destructors while an indexing worker may still be
  // using the objects they tear down, which can hang the process the editor is
  // waiting on.
  std::fflush(nullptr);
  std::_Exit(shutdown_requested_ ? 0 : 1);
}
}  // namespace kura::lsp
