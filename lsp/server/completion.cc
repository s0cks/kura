#include "lsp/server/completion.h"

#include "frontend/syntax/source.h"
#include "lsp/completion_item.h"
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

  const auto offset = doc->GetTree()->GetSource().GetOffset(syntax::Position{
      .line = static_cast<uint32_t>(params.position.line),
      .column = static_cast<uint32_t>(params.position.character),
  });

  std::vector<CompletionItem> items{};

  const auto& text = doc->GetTree()->GetText(syntax::Span(offset - 1, offset + 1));
  log::info(std::format("text: {}", text));

  if (doc->GetTree()->GetText(syntax::Span(offset - 1, offset + 1)) == "fn") {
    items.push_back(CompletionItem{
        .kind = CompletionItem::kSnippet,
        .label = "fn",
        .detail = "Function signature definition",
        .insertText = "fn ${1:name}(${2:args}): ${3:returnType} => \n\t$0\n",
        .insertTextFormat = CompletionItem::kSnippetFormat,
    });
  }

  return Send(req.id, items);
}
}  // namespace kura::lsp
