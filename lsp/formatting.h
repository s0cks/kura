#ifndef KURA_LSP_FORMATTING_H
#define KURA_LSP_FORMATTING_H

#include <expected>
#include <optional>
#include <vector>

#include "lsp/document.h"
#include "lsp/types.h"

namespace kura::lsp {
struct FormattingOptions {
  int tab_size = 2;
  bool insert_final_newline = true;
};

// The edits that make the file look the way `kura fmt` would write it, or why
// it can't be formatted: a file with syntax errors is left alone, because the
// formatter would be guessing at what was meant.
//
// With a `range`, only the changes that touch it are returned.
auto FormatDocument(Document& doc, const FormattingOptions& options, const std::optional<Range>& range = std::nullopt)
    -> std::expected<std::vector<TextEdit>, std::string>;
}  // namespace kura::lsp

#endif  // KURA_LSP_FORMATTING_H
