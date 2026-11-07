#include "lsp/params.h"

#include <jansson.h>

#include "common.h"

namespace kura::lsp {

auto TextDocumentPositionParams::operator<<(const json_t* doc) -> TextDocumentPositionParams& {
  KASSERT(doc);
  json_t* textDocument_node = json_object_get(doc, "textDocument");
  if (textDocument_node)
    textDocument << textDocument_node;

  json_t* position_node = json_object_get(doc, "position");
  if (position_node)
    FromJson(position_node, position);

  return *this;
}
}  // namespace kura::lsp
