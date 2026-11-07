#ifndef KURA_LSP_DOCUMENT_SYMBOLS_H
#define KURA_LSP_DOCUMENT_SYMBOLS_H

#include "lsp/types.h"
namespace kura::lsp {
struct DocumentSymbolsParams {
  TextDocument textDocument;

  auto operator<<(const json_t* doc) -> DocumentSymbolsParams&;
};
}  // namespace kura::lsp

#endif  // KURA_LSP_DOCUMENT_SYMBOLS_H
