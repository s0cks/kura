#ifndef KURA_LSP_CODE_ACTION_H
#define KURA_LSP_CODE_ACTION_H

#include <string>
#include <vector>

#include "lsp/document.h"
#include "lsp/types.h"

namespace kura::lsp {
// A `quickfix` that applies `edits` to the document it was computed for.
struct CodeAction {
  std::string title{};
  std::string kind = "quickfix";
  bool isPreferred = false;
  URI uri{};
  std::vector<TextEdit> edits{};

  void operator>>(json_t* rhs) const;
};

// Quick fixes for the problems the parser found within `range`:
//
//  * "Add an empty parameter list" for `fn name => ...` (E0009), which turns
//    it into `fn name() => ...`.
//
// The fixes come from the document's own syntax tree, so the client does not
// need to have been sent diagnostics for them to show up. When the tree is
// older than the text (an index is still running) there are none, rather than
// edits for text that is no longer there.
auto GetCodeActions(const Document& doc, const Range& range) -> std::vector<CodeAction>;
}  // namespace kura::lsp

#endif  // KURA_LSP_CODE_ACTION_H
