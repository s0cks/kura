#include "lsp/error.h"

namespace kura::lsp {
void Error::operator>>(json_t* doc) const {
  json_object_set_new(doc, "code", json_integer(code));
  json_object_set_new(doc, "message", json_string(message.data()));
}
}  // namespace kura::lsp
