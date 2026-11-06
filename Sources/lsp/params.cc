#include "lsp/params.h"

#include <jansson.h>

#include "common.h"

namespace kura::lsp {
void FromJson(json_t* doc, TextDocumentPositionParams& params) {
  KASSERT(doc);
  json_t* textDocument_node = json_object_get(doc, "textDocument");
  if (textDocument_node)
    params.textDocument = TextDocument(textDocument_node);

  json_t* position_node = json_object_get(doc, "position");
  if (position_node)
    FromJson(position_node, params.position);
}
}  // namespace kura::lsp
