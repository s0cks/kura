#ifndef KURA_LSP_BINDER_H
#define KURA_LSP_BINDER_H

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "frontend/syntax/ast.h"

namespace kura::lsp {
// What a name was declared as.
enum class DeclKind : uint8_t {
  kFn,
  kExtern,
  kType,
  kTypeParam,
  kVariant,
  kField,
  kLet,
  kParam,
  kLambdaParam,
  kForIndex,
  kForElement,
  kAccumulator,
  kPatternBinding,
  kAlias,
};

// Types, variants and type parameters live in one namespace, everything else
// that can be called or read lives in another, and record fields are looked up
// by the record, never by scope.
enum class Ns : uint8_t { kValue, kType, kField };

auto GetNamespace(DeclKind kind) -> Ns;
auto ToString(DeclKind kind) -> std::string_view;

struct Decl {
  uint32_t id = 0;
  DeclKind kind = DeclKind::kLet;
  // Without the backticks of a raw name.
  std::string name{};
  Span name_span{};
  // The whole declaration, for outlines and for "where does this end".
  Span span{};
  const syntax::Node* node = nullptr;
  // The type a variant or field belongs to, or the function a parameter
  // belongs to. -1 for the rest.
  int32_t parent = -1;
  uint32_t scope = 0;
  // Names inside a block only exist after their statement.
  uint32_t visible_from = 0;
  // `use "math.h"` with no `as`: the alias is made up, so it can't be renamed.
  bool implicit = false;
  bool raw = false;
};

// How a name is written at a place that is not a plain identifier.
enum class Pun : uint8_t {
  kNone,
  // `#el(width:)` or `{ width: }`: the name stands for `width: width`.
  kArg,
  // `{ width }` in a pattern: the name is the field and the new binding.
  kPattern,
};

// One place a declared name shows up: the declaration itself, or a use.
struct Occurrence {
  Span span{};
  uint32_t decl = 0;
  bool is_declaration = false;
  // A use that was matched by name alone: a record field is found by its
  // name when only one record in the file has a field of that name. Good
  // enough to jump to, not safe to rewrite.
  bool is_guess = false;
  Pun pun = Pun::kNone;
  // `kPun` Args: where the text after the colon goes.
  uint32_t pun_end = 0;
};

// `hp.percent` or `hp.Health`: a name that is looked up in the file the alias
// `hp` stands for, which is not this one.
struct QualifiedRef {
  Span span{};
  uint32_t alias = 0;
  std::string name{};
  Ns ns = Ns::kValue;
};

struct Scope {
  Span span{};
  int32_t parent = -1;
  std::vector<uint32_t> decls{};
};

class Bindings {
  friend class Binder;

 private:
  std::vector<Decl> decls_{};
  std::vector<Scope> scopes_{};
  // Sorted by span begin; used to find what is under the cursor.
  std::vector<Occurrence> occurrences_{};
  std::unordered_map<std::string, std::vector<uint32_t>> fields_by_name_{};
  std::vector<Span> unresolved_{};
  std::unordered_map<uint32_t, const syntax::Node*> decl_types_{};
  std::vector<QualifiedRef> qualified_{};

 public:
  static auto Build(const syntax::Module* root) -> std::unique_ptr<Bindings>;

  auto GetDecls() const -> const std::vector<Decl>& {
    return decls_;
  }

  auto GetDecl(const uint32_t id) const -> const Decl& {
    return decls_[id];
  }

  auto GetOccurrences() const -> const std::vector<Occurrence>& {
    return occurrences_;
  }

  auto GetScopes() const -> const std::vector<Scope>& {
    return scopes_;
  }

  // The declaration or use whose name the offset is on (or right after).
  auto FindOccurrenceAt(uint32_t offset) const -> const Occurrence*;

  // Every occurrence of `decl`, declaration first. Guesses are left out
  // unless asked for.
  auto GetOccurrences(uint32_t decl, bool include_guesses) const -> std::vector<const Occurrence*>;

  // The innermost scope that holds the offset.
  auto FindScopeAt(uint32_t offset) const -> int32_t;

  // What `name` means at `offset`, looking outward from the innermost scope.
  auto Lookup(std::string_view name, Ns ns, uint32_t offset) const -> const Decl*;

  // Everything that can be named at `offset`, innermost first. An inner name
  // hides an outer one of the same name.
  auto GetVisibleAt(uint32_t offset, Ns ns) const -> std::vector<const Decl*>;

  // The variants and fields declared inside a type.
  auto GetChildren(uint32_t decl) const -> std::vector<const Decl*>;

  auto GetFieldsNamed(std::string_view name) const -> const std::vector<uint32_t>*;

  auto GetQualifiedRefs() const -> const std::vector<QualifiedRef>& {
    return qualified_;
  }

  auto FindQualifiedRefAt(const uint32_t offset) const -> const QualifiedRef* {
    for (const auto& q : qualified_) {
      if (q.span.Touches(offset))
        return &q;
    }
    return nullptr;
  }

  // The declaration made for `node`, such as a FnDecl or a TypeDecl.
  auto FindDeclOf(const syntax::Node* node) const -> const Decl* {
    for (const auto& d : decls_) {
      if (d.node == node)
        return &d;
    }
    return nullptr;
  }

  // The type a name was written with, or that could be told from where it
  // came from (a payload of a variant, a field), as the type node of the
  // tree. Null when it is not known.
  auto GetDeclType(uint32_t decl) const -> const syntax::Node* {
    const auto it = decl_types_.find(decl);
    return it == decl_types_.end() ? nullptr : it->second;
  }
};

// The names Kura has without any declaration: `Int`, `Seq`, `Node`...
auto IsBuiltinType(std::string_view name) -> bool;
auto GetBuiltinTypes() -> const std::vector<std::string_view>&;
}  // namespace kura::lsp

#endif  // KURA_LSP_BINDER_H
