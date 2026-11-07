#include "lsp/server/initialize.h"

#include "common.h"
#include "lsp/server/server.h"

namespace kura::lsp {
void InitializeResult::operator>>(json_t* doc) const {
  KASSERT(doc);

  json_t* info_node = json_object();
  if (info_node) {
    info >> info_node;
    json_object_set_new(doc, "serverInfo", info_node);
  }

  json_t* caps_node = json_object();
  if (caps_node) {
    capabilities >> caps_node;
    json_object_set_new(doc, "capabilities", caps_node);
  }
}

void Server::HandleInitialize(const Request& req) {
  return Send(req.id, InitializeResult{
                          .info = info_,
                          .capabilities = caps_,
                      });
}
}  // namespace kura::lsp
