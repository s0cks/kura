#include "lsp/completion_item.h"

#include "common.h"

namespace kura::lsp {
void CompletionItem::operator>>(json_t* doc) const {
  KASSERT(doc);
  json_object_set(doc, "kind", json_integer(kind));
  if (!label.empty())
    json_object_set(doc, "label", json_string(label.c_str()));
  if (!detail.empty())
    json_object_set(doc, "detail", json_string(detail.c_str()));
  json_object_set(doc, "preselect", json_boolean(preselect));
  if (!sort.empty())
    json_object_set(doc, "sortText", json_string(sort.c_str()));
  if (!filter.empty())
    json_object_set(doc, "filterText", json_string(filter.c_str()));
  if (insertTextFormat != kNoInsertTextFormat)
    json_object_set(doc, "insertTextFormat", json_integer(insertTextFormat));
  if (!insertText.empty())
    json_object_set(doc, "insertText", json_string(insertText.c_str()));

  // TODO(@s0cks):
  // MarkupContent documentation{};
}

void CompletionList::operator>>(json_t* doc) const {
  json_object_set(doc, "isIncomplete", json_boolean(isIncomplete));

  json_t* items_node = json_array();
  KASSERT(items_node);
  for (const auto& i : items) {
    json_t* item = json_object();
    KASSERT(item);
    i >> item;
    json_array_append(items_node, item);
  }
}
}  // namespace kura::lsp
