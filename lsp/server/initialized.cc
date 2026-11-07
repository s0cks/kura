#include "lsp/server/initialized.h"

#include "lsp/server/server.h"

namespace kura::lsp {
void Server::HandleInitialized(const Request& req) {
  log::info("client initialized");
}
}  // namespace kura::lsp
