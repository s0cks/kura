#include "lsp/server/exit.h"

#include "lsp/server/server.h"

namespace kura::lsp {
void Server::HandleExit(const Request& req) {
  log::info("exit");
  std::exit(0);
}
}  // namespace kura::lsp
