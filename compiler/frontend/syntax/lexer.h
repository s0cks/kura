#ifndef KURA_SYNTAX_LEXER_H
#define KURA_SYNTAX_LEXER_H

#include <vector>

#include "compiler/frontend/syntax/diagnostic.h"
#include "compiler/frontend/syntax/source.h"
#include "compiler/frontend/syntax/token.h"

namespace kura::syntax {
struct LexResult {
  // Always ends with exactly one kEof token.
  std::vector<Token> tokens{};
  // `// ...` comments. They are not tokens, but tools (the formatter, the LSP)
  // need to find them again.
  std::vector<Span> comments{};
};

// Splits a source file into tokens. Lexing never fails: a problem becomes a
// diagnostic and the lexer carries on, so the parser (and with it the LSP)
// always gets a complete token stream.
//
//  - A string or backtick name that is not closed is still emitted as a
//    string / raw name, up to the end of the line.
//  - A character Kura has no use for becomes one kInvalid token.
//
// `a - b` is a subtraction only with whitespace on both sides of the `-`; the
// lexer tells the parser by emitting kBinMinus rather than kMinus.
auto Lex(const SourceFile& source, DiagnosticList& diagnostics) -> LexResult;
}  // namespace kura::syntax

#endif  // KURA_SYNTAX_LEXER_H
