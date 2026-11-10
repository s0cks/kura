#ifndef KURA_LSP_KEYWORD_COMPLETION_H
#define KURA_LSP_KEYWORD_COMPLETION_H

#include <cstdint>
#include <string_view>
#include <vector>

#include "lsp/completion_item.h"
#include "lsp/document.h"

namespace kura::lsp {
// The identifier-ish word that ends at the cursor, e.g. `fn` in `fn|`.
struct WordAtCursor {
  uint32_t begin = 0;
  uint32_t end = 0;
  std::string_view text{};

  // Nothing but whitespace between the start of the line and the word.
  bool at_line_start = true;
};

// Finds the word that ends at `offset`. Words are made of letters, digits, `_`
// and `-` (kebab-case names are one word), so `my-fn` is a single word.
auto GetWordAtCursor(std::string_view text, uint32_t offset) -> WordAtCursor;

// True when `offset` is not inside any `()`, `[]` or `{}`. Strings and `//`
// comments are skipped, so a brace in either does not count.
auto IsTopLevel(std::string_view text, uint32_t offset) -> bool;

// `fn` and `type` snippets for declaration position: at the top level, at the
// start of a line, where the word typed so far is a prefix of the keyword.
// Anywhere else this returns nothing.
auto GetDeclarationCompletions(const Document& doc, uint32_t offset) -> std::vector<CompletionItem>;
}  // namespace kura::lsp

#endif  // KURA_LSP_KEYWORD_COMPLETION_H
