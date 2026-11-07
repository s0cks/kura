#ifndef KURA_LSP_REFERENCES_H
#define KURA_LSP_REFERENCES_H

#include "lsp/types.h"

namespace kura::lsp {
struct ReferenceParams {
  TextDocument textDocument;
  Position position;

  auto operator<<(const json_t* doc) -> ReferenceParams&;
};
}  // namespace kura::lsp

#endif  // KURA_LSP_REFERENCES_H
