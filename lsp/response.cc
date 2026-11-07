#include "lsp/response.h"

#include <jansson.h>

#include "common.h"

namespace kura::lsp {
void InitializeResponse::operator>>(json_t* doc) const {
  KASSERT(doc);

  json_t* info_node = json_object();
  if (info_node) {
    info >> info_node;
    json_object_set_new(doc, "serverInfo", info_node);
  }

  json_t* caps_node = json_object();
  if (caps_node) {
    capabilities >> caps_node;
    json_object_set_new(doc, "capabilities", caps_node);
  }
}

void Response::operator>>(json_t* doc) const {
  KASSERT(doc);
  json_object_set_new(doc, "jsonrpc", json_string("2.0"));
  json_object_set_new(doc, "id", json_integer(id));

  json_t* val = json_object();
  if (const auto* err = std::get_if<Error>(&value)) {
    (*err) >> val;

    json_object_set_new(doc, "error", val);
    return;
  } else if (const auto* res = std::get_if<ResponseType>(&value)) {
    if (const auto* init_resp = std::get_if<InitializeResponse>(res)) {
      (*init_resp) >> val;
    } else if (const auto* hover_resp = std::get_if<Hover>(res)) {
      (*hover_resp) >> val;
    }

    json_object_set_new(doc, "result", val);
    return;  // TODO(@s0cks): serialize
  }

  std::fprintf(stderr, "invalid response shape\n");
  std::exit(1);
}
}  // namespace kura::lsp
