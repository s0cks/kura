#include "lsp/symbol.h"

#include "common.h"

namespace kura::lsp {
void Symbol::operator>>(json_t* doc) const {
  json_object_set(doc, "name", json_string(name.c_str()));
  json_object_set(doc, "detail", json_string(detail.c_str()));
  json_object_set(doc, "kind", json_integer(kind));

  json_t* range_node = json_object();
  KASSERT(range_node);
  range >> range_node;
  json_object_set(doc, "range", range_node);

  json_t* selectionRange_node = json_object();
  KASSERT(selectionRange_node);
  selectionRange >> selectionRange_node;
  json_object_set(doc, "selectionRange", selectionRange_node);
}

auto Symbol::operator<<(const json_t* doc) -> Symbol& {
  const auto* name_node = json_object_get(doc, "name");
  if (name_node)
    name = json_string_value(name_node);

  const auto* detail_node = json_object_get(doc, "detail");
  if (detail_node)
    detail = json_string_value(detail_node);

  const auto* kind_node = json_object_get(doc, "kind");
  if (kind_node)
    kind = static_cast<Kind>(json_integer_value(kind_node));

  const auto* range_node = json_object_get(doc, "range");
  if (range_node)
    range << range_node;

  const auto* selectionRange_node = json_object_get(doc, "selectionRange");
  if (selectionRange_node)
    selectionRange << selectionRange_node;

  return *this;
}
}  // namespace kura::lsp
