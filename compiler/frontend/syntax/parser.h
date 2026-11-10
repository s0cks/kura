#ifndef KURA_SYNTAX_PARSER_H
#define KURA_SYNTAX_PARSER_H

#include <memory>
#include <vector>

#include "compiler/frontend/syntax/ast.h"
#include "compiler/frontend/syntax/diagnostic.h"
#include "compiler/frontend/syntax/source.h"
#include "compiler/frontend/syntax/token.h"

namespace kura::syntax {
// Everything the front end knows about one source file. Owns the text, the
// tokens, the comments, every AST node and every diagnostic, so a SyntaxTree
// can be handed around (to the ExprBuilder, to the LSP, across CGo) as a unit.
class SyntaxTree {
  friend class TreeBuilder;

 private:
  std::shared_ptr<const SourceFile> source_;
  Module* root_ = nullptr;
  std::vector<Token> tokens_{};
  std::vector<Span> comments_{};
  DiagnosticList diagnostics_{};

 public:
  explicit SyntaxTree(std::shared_ptr<const SourceFile> source) :
    source_(std::move(source)) {}
  ~SyntaxTree() = default;
  SyntaxTree(const SyntaxTree&) = delete;
  SyntaxTree& operator=(const SyntaxTree&) = delete;

  auto GetSource() const -> const SourceFile& {
    return *source_;
  }

  auto GetSourcePtr() const -> const std::shared_ptr<const SourceFile>& {
    return source_;
  }

  // Never null, even for a file full of errors.
  auto GetRoot() const -> const Module* {
    return root_;
  }

  auto GetTokens() const -> const std::vector<Token>& {
    return tokens_;
  }

  auto GetComments() const -> const std::vector<Span>& {
    return comments_;
  }

  auto GetDiagnostics() const -> const DiagnosticList& {
    return diagnostics_;
  }

  // True when the tree is exactly what was written, with no ErrorNodes in it.
  auto IsValid() const -> bool {
    return !diagnostics_.HasErrors();
  }

  auto GetText(const Span& span) const -> std::string_view {
    return source_->GetText(span);
  }

  auto GetText(const Node* node) const -> std::string_view {
    return source_->GetText(node->span);
  }
};

struct ParseOptions {
  // Expressions, types, patterns and elements nested deeper than this are an
  // error (roughly half this many levels of parentheses). Keeps hostile input,
  // and fuzzers, from overflowing the stack.
  uint32_t max_depth = 256;
};

// Parses a source file. Always returns a tree; check `IsValid()` or the
// diagnostics to learn whether the file was well formed.
auto Parse(std::shared_ptr<const SourceFile> source, const ParseOptions& options = {}) -> std::unique_ptr<SyntaxTree>;
auto ParseText(std::string name, std::string text, const ParseOptions& options = {}) -> std::unique_ptr<SyntaxTree>;
}  // namespace kura::syntax

#endif  // KURA_SYNTAX_PARSER_H
