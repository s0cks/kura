#ifndef KURA_LSP_RESPONSE_H
#define KURA_LSP_RESPONSE_H

#include "lsp/error.h"
#include "lsp/types.h"

namespace kura::lsp {
struct InitializeResponse {
  ServerInfo info;
  ServerCapabilities capabilities;
};

void ToJson(json_t* doc, const InitializeResponse& resp);

using ResponseType = std::variant<InitializeResponse, Hover>;

struct Response {
  long id;
  std::variant<ResponseType, Error> value{};
};

void ToJson(json_t* doc, const Response& resp);
}  // namespace kura::lsp

#endif  // KURA_LSP_RESPONSE_H
