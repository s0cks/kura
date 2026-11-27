#ifndef KURA_LSP_HOVER_H
#define KURA_LSP_HOVER_H

#include <optional>

#include "lsp/document.h"
#include "lsp/modules.h"
#include "lsp/types.h"

namespace kura::lsp {
// What the editor shows for the name under the cursor: what it was declared
// as and, for an element or one of its properties, what it accepts. Nothing
// when the cursor isn't on anything that has something to say.
auto GetHover(Document& doc, const Position& pos, ModuleResolver* modules = nullptr) -> std::optional<Hover>;
}  // namespace kura::lsp

#endif  // KURA_LSP_HOVER_H
