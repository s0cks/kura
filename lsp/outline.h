#ifndef KURA_LSP_OUTLINE_H
#define KURA_LSP_OUTLINE_H

#include <vector>

#include "lsp/document.h"
#include "lsp/symbol.h"

namespace kura::lsp {
// The declarations of the file as a tree: types with their fields and
// variants, functions with their signatures, `use`s, tests.
auto GetOutline(Document& doc) -> std::vector<Symbol>;

struct FoldingRange {
  int start_line = 0;
  int end_line = 0;
  // "comment", "imports" or empty for code.
  std::string kind{};
};

// What can be collapsed: declarations, braced bodies, records, multi-line
// elements, runs of `//` comments and of `use` lines.
auto GetFoldingRanges(Document& doc) -> std::vector<FoldingRange>;
}  // namespace kura::lsp

#endif  // KURA_LSP_OUTLINE_H
