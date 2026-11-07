#ifndef KURA_LSP_RESPONSE_H
#define KURA_LSP_RESPONSE_H

#include "lsp/completion_item.h"
#include "lsp/error.h"
#include "lsp/server/initialize.h"
#include "lsp/types.h"

namespace kura::lsp {
using ResponseType = std::variant<InitializeResult, Hover, CompletionList>;

struct Response {
  long id;
  std::variant<ResponseType, Error> value{};

  void operator>>(json_t* rhs) const;
};
}  // namespace kura::lsp

#endif  // KURA_LSP_RESPONSE_H
