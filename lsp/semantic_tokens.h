#ifndef KURA_LSP_SEMANTIC_TOKENS_H
#define KURA_LSP_SEMANTIC_TOKENS_H

#include <cstdint>
#include <vector>

#include "lsp/document.h"
#include "lsp/types.h"

namespace kura::lsp {
// The legend the tokens are numbered by, announced to the client once.
auto GetSemanticTokensLegend() -> SemanticTokensLegend;

// The tokens of the whole file as the protocol wants them: five integers per
// token (line and column relative to the one before, length, type, modifiers).
//
// What is coloured is what the compiler knows and a grammar can't: whether a
// name is a function or a parameter, a type or a variant, a field, an element,
// one of an element's properties.
auto GetSemanticTokens(Document& doc) -> std::vector<uint32_t>;
}  // namespace kura::lsp

#endif  // KURA_LSP_SEMANTIC_TOKENS_H
