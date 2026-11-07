#include "lsp/server/document_symbols.h"

#include <format>

#include "lsp/server/server.h"

namespace kura::lsp {
auto DocumentSymbolsParams::operator<<(const json_t* doc) -> DocumentSymbolsParams& {
  json_t* textDocument_node = json_object_get(doc, "textDocument");
  if (textDocument_node)
    textDocument << textDocument_node;
  return *this;
}

void Server::HandleDocumentSymbols(const Request& req) {
  DocumentSymbolsParams params{};
  params << req.params;

  auto* doc = docs_.Get(params.textDocument.uri);
  if (!doc)
    return SendInternalError(req.id, std::format("document {} not indexed", params.textDocument.uri));

  std::vector<Symbol> symbols{};
  doc->GetSymbols(symbols);

  Send(req.id, symbols);
}
}  // namespace kura::lsp
