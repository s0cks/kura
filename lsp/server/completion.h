#ifndef KURA_LSP_SERVER_COMPLETION_H
#define KURA_LSP_SERVER_COMPLETION_H

#include "lsp/types.h"

namespace kura::lsp {
struct CompletionParams {
  TextDocument textDocument;
  Position position;

  auto operator<<(const json_t* doc) -> CompletionParams&;
};
}  // namespace kura::lsp

#endif  // KURA_LSP_SERVER_COMPLETION_H
