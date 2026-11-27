#ifndef KURA_LSP_NAVIGATION_H
#define KURA_LSP_NAVIGATION_H

#include <expected>
#include <optional>
#include <string>
#include <vector>

#include "lsp/document.h"
#include "lsp/modules.h"
#include "lsp/types.h"

namespace kura::lsp {
// Go to definition: where the name under the cursor is declared, or nothing
// when it isn't declared in this file (a builtin, a name in a header).
struct Target {
  URI uri{};
  Range range{};
};

// A name from another file (`hp.percent`) is followed into that file.
auto GetDefinition(Document& doc, const Position& pos, ModuleResolver* modules = nullptr) -> std::optional<Target>;

// Every place the name under the cursor is declared or used.
auto GetReferences(Document& doc, const Position& pos, bool include_declaration) -> std::vector<Range>;

struct Highlight {
  enum Kind : int { kText = 1, kRead = 2, kWrite = 3 };
  Range range{};
  Kind kind = kRead;
};

// The same name elsewhere in the file, for the editor to light up.
auto GetHighlights(Document& doc, const Position& pos) -> std::vector<Highlight>;

struct RenameTarget {
  Range range{};
  std::string placeholder{};
};

// What would be renamed, or why it can't be.
auto PrepareRename(Document& doc, const Position& pos) -> std::expected<RenameTarget, std::string>;

// The edits that rename the name under the cursor, or why they can't be made.
auto Rename(Document& doc, const Position& pos, const std::string& new_name)
    -> std::expected<std::vector<TextEdit>, std::string>;
}  // namespace kura::lsp

#endif  // KURA_LSP_NAVIGATION_H
