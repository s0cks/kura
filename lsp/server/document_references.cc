#include "lsp/server/document_references.h"

#include <jansson.h>

#include "lsp/server/server.h"

namespace kura::lsp {
auto ReferenceParams::operator<<(const json_t* doc) -> ReferenceParams& {
  json_t* textDocument_node = json_object_get(doc, "textDocument");
  if (textDocument_node)
    textDocument << textDocument_node;

  json_t* position_node = json_object_get(doc, "position");
  if (position_node)
    position << position_node;

  return *this;
}

void Server::HandleReferences(const Request& req) {
  SendInternalError(req.id, "No references found");
}
}  // namespace kura::lsp
