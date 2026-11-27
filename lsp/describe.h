#ifndef KURA_LSP_DESCRIBE_H
#define KURA_LSP_DESCRIBE_H

#include <string>
#include <string_view>

#include "frontend/syntax/ast.h"
#include "lsp/binder.h"

namespace kura::lsp {
// A type as it is written: `Seq<Int>`, `{ x: Float, y: Float }`, `(Int) => Int`.
auto TypeToString(const syntax::Node* type) -> std::string;

// `(a: Int, b: String): Node`
auto ParamsToString(const std::vector<syntax::Node*>& params, bool variadic = false) -> std::string;

// `fn name(a: Int): Node`
auto SignatureOf(const syntax::FnDecl& fn) -> std::string;
auto SignatureOf(const syntax::ExternDecl& ext) -> std::string;

// `type State = { ... }` over several lines when it doesn't fit on one.
auto DeclarationOf(const syntax::TypeDecl& type) -> std::string;

// The `//` comment lines directly above the line `offset` is on, with the
// slashes taken off. Empty when there is none.
auto LeadingComment(std::string_view text, uint32_t offset) -> std::string;

// Markdown for hovering over the name `decl`.
auto DescribeDecl(const Bindings& bindings, const Decl& decl, std::string_view text) -> std::string;

// One line about a built-in type, empty for a name that isn't one.
auto DescribeBuiltinType(std::string_view name) -> std::string_view;
}  // namespace kura::lsp

#endif  // KURA_LSP_DESCRIBE_H
