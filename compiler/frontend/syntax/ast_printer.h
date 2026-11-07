#ifndef KURA_SYNTAX_AST_PRINTER_H
#define KURA_SYNTAX_AST_PRINTER_H

#include <iosfwd>
#include <string>

#include "compiler/frontend/syntax/ast.h"
#include "compiler/frontend/syntax/parser.h"

namespace kura::syntax {
struct PrintOptions {
  // Append each node's [begin, end) byte span.
  bool spans = false;
};

// An indented dump of a tree, one node per line, with the details that matter
// for that node (names, operators, literal text). The output is stable, so it
// doubles as a golden file format for the parser tests.
void PrintTree(std::ostream& out, const SyntaxTree& tree, const PrintOptions& options = {});
auto PrintTreeToString(const SyntaxTree& tree, const PrintOptions& options = {}) -> std::string;
void PrintTokens(std::ostream& out, const SyntaxTree& tree);
}  // namespace kura::syntax

#endif  // KURA_SYNTAX_AST_PRINTER_H
