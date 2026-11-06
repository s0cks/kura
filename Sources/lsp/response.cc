#include "lsp/response.h"

#include <jansson.h>

#include "common.h"

namespace kura::lsp {
void ToJson(json_t* doc, const InitializeResponse& res) {
  KASSERT(doc);

  json_t* info = json_object();
  if (info) {
    ToJson(info, res.info);
    json_object_set_new(doc, "serverInfo", info);
  }

  json_t* caps = json_object();
  if (caps) {
    ToJson(caps, res.capabilities);
    json_object_set_new(doc, "capabilities", caps);
  }
}

void ToJson(json_t* doc, const Response& resp) {
  KASSERT(doc);
  json_object_set_new(doc, "jsonrpc", json_string("2.0"));
  json_object_set_new(doc, "id", json_integer(resp.id));

  json_t* value = json_object();
  if (const auto* err = std::get_if<Error>(&resp.value)) {
    json_object_set_new(value, "code", json_integer(err->code));
    json_object_set_new(value, "message", json_string(err->message.data()));

    json_object_set_new(doc, "error", value);
    return;
  } else if (const auto* res = std::get_if<ResponseType>(&resp.value)) {
    if (const auto* init_resp = std::get_if<InitializeResponse>(res)) {
      ToJson(value, *init_resp);
    } else if (const auto* hover_resp = std::get_if<Hover>(res)) {
      ToJson(value, *hover_resp);
    }

    json_object_set_new(doc, "result", value);
    return;  // TODO(@s0cks): serialize
  }

  std::fprintf(stderr, "invalid response shape\n");
  std::exit(1);
}
}  // namespace kura::lsp
