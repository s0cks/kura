#include "lsp/diagnostic.h"

#include "common.h"

namespace kura::lsp {
void Diagnostic::operator>>(json_t* doc) const {
  KASSERT(doc);
  json_t* range_node = json_object();
  if (range_node) {
    range >> range_node;
    json_object_set(doc, "range", range_node);
  }

  json_object_set(doc, "severity", json_integer(severity));
  json_object_set(doc, "code", json_string(std::format("K{}", code).c_str()));

  if (source)
    json_object_set(doc, "source", json_string(source->c_str()));

  json_t* message_node = json_object();
  if (message_node) {
    message >> message_node;
    json_object_set(doc, "message", message_node);
  }
}
}  // namespace kura::lsp
