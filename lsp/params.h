#ifndef KURA_LSP_PARAMS_H
#define KURA_LSP_PARAMS_H

#include "lsp/types.h"

namespace kura::lsp {
struct TextDocumentPositionParams {
  TextDocument textDocument;
  Position position;

  auto operator<<(const json_t* doc) -> TextDocumentPositionParams&;
};
}  // namespace kura::lsp

#endif  // KURA_LSP_PARAMS_H
