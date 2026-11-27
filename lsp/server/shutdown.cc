#include "lsp/server/shutdown.h"

#include "lsp/server/server.h"

namespace kura::lsp {
void Server::HandleShutdown(const Request& req) {
  log::info("shutdown");
  shutdown_requested_ = true;
  // The reply to `shutdown` is `null`; the client sends `exit` after it.
  SendNull(req.id);
}
}  // namespace kura::lsp
