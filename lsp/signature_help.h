#ifndef KURA_LSP_SIGNATURE_HELP_H
#define KURA_LSP_SIGNATURE_HELP_H

#include <optional>
#include <string>
#include <vector>

#include "lsp/document.h"
#include "lsp/modules.h"
#include "lsp/types.h"

namespace kura::lsp {
struct SignatureHelp {
  std::string label{};
  std::string documentation{};
  std::vector<std::string> parameters{};
  // -1 when no parameter can be told to be the one being written.
  int active_parameter = -1;
};

// The signature of the function, variant or element whose parentheses the
// cursor is in, with the argument being written marked.
auto GetSignatureHelp(Document& doc, const Position& pos, ModuleResolver* modules = nullptr) -> std::optional<SignatureHelp>;
}  // namespace kura::lsp

#endif  // KURA_LSP_SIGNATURE_HELP_H
