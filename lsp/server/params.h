#ifndef KURA_LSP_PARAMS_H
#define KURA_LSP_PARAMS_H

#include "lsp/types.h"

namespace kura::lsp {
struct TextDocumentPositionParams {
  TextDocument textDocument;
  Position position;

  auto operator<<(const json_t* doc) -> TextDocumentPositionParams&;
};

enum CodeActionKind {

};

struct CodeActionContext {
  // TODO(@s0cks): diagnostics
  // TODO(@s0cks): only --- a bitset of CodeActionKind
};

struct CodeActionParams {
  TextDocument textDocument;
  Range range;

  auto operator<<(const json_t* doc) -> CodeActionParams&;
};
}  // namespace kura::lsp

#endif  // KURA_LSP_PARAMS_H
