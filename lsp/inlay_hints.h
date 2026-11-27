#ifndef KURA_LSP_INLAY_HINTS_H
#define KURA_LSP_INLAY_HINTS_H

#include <string>
#include <vector>

#include "lsp/document.h"
#include "lsp/types.h"

namespace kura::lsp {
struct InlayHint {
  enum Kind : int { kType = 1, kParameter = 2 };

  Position position{};
  std::string label{};
  Kind kind = kType;
  bool padding_left = false;
  bool padding_right = false;
};

// Small notes in the text that aren't in the file: the type of a name that was
// not given one (`let size = ...` is `: Size`, a payload taken apart in a
// pattern), and the name of the parameter a call's literal argument goes to.
auto GetInlayHints(Document& doc, const Range& range) -> std::vector<InlayHint>;
}  // namespace kura::lsp

#endif  // KURA_LSP_INLAY_HINTS_H
