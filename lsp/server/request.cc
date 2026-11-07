#include "lsp/server/request.h"

namespace kura::lsp {
Request::Request(const char* data, const size_t len) {
  doc = json_loadb(data, len, 0, &error);
  if (!doc)
    return;

  json_t* id_node = json_object_get(doc, "id");
  id = id_node ? json_integer_value(id_node) : 0;

  json_t* method_node = json_object_get(doc, "method");
  if (!method_node || !json_is_string(method_node))
    return;

  method = json_string_value(method_node);
  params = json_object_get(doc, "params");
}

Request::~Request() {
  if (doc)
    json_decref(doc);
}
}  // namespace kura::lsp
