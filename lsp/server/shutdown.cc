#include "lsp/server/shutdown.h"

#include "lsp/server/server.h"

namespace kura::lsp {
void Server::HandleShutdown(const Request& req) {
  log::info("shutdown");
}
}  // namespace kura::lsp
