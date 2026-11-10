#include "lsp/server/completion.h"

#include "lsp/completion_item.h"
#include "lsp/keyword_completion.h"
#include "lsp/server/server.h"

namespace kura::lsp {
auto CompletionParams::operator<<(const json_t* doc) -> CompletionParams& {
  json_t* textDocument_node = json_object_get(doc, "textDocument");
  if (textDocument_node)
    textDocument << textDocument_node;

  json_t* position_node = json_object_get(doc, "position");
  if (position_node)
    position << position_node;
  return *this;
}

void Server::HandleCompletion(const Request& req) {
  CompletionParams params{};
  params << req.params;

  auto* doc = docs_.Get(params.textDocument.uri);
  if (!doc)
    return SendInternalError(req.id, std::format("document {} not indexed", params.textDocument.uri));

  return Send(req.id, GetDeclarationCompletions(*doc, doc->ToOffset(params.position)));
}
}  // namespace kura::lsp
