#ifndef KURA_LSP_HOVER_H
#define KURA_LSP_HOVER_H

#include <sstream>

#include "frontend/syntax/parser.h"

namespace kura::lsp {
auto ToHoverInfo(std::stringstream& ss, const syntax::Node* node) -> bool;
}

#endif  // KURA_LSP_HOVER_H
