#ifndef KURA_SYNTAX_AST_H
#define KURA_SYNTAX_AST_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "frontend/syntax/diagnostic.h"
#include "frontend/syntax/source.h"
#include "frontend/syntax/token.h"

namespace kura::syntax {
// The syntax tree is *only* syntax: it mirrors what was written, keeps a Span
// on everything, and knows nothing about scopes, types or C headers. Name
// resolution and the rest of the meaning live in the ExprBuilder, which walks
// this tree. That split is what lets the LSP get a tree (with ErrorNodes in the
// broken places) out of a file that doesn't compile.
//
// clang-format off
#define FOR_EACH_NODE(V) \
  V(Error)               \
  V(Module)              \
  V(Directive)           \
  V(Arg)                 \
  V(Param)               \
  V(Field)               \
  V(TypeDecl)            \
  V(FnDecl)              \
  V(ExternDecl)          \
  V(UseDecl)             \
  V(Binding)             \
  V(NamedType)           \
  V(VariantType)         \
  V(QualifiedType)       \
  V(NoneType)            \
  V(RecordType)          \
  V(FunctionType)        \
  V(UnionType)           \
  V(NumberLit)           \
  V(StringLit)           \
  V(BoolLit)             \
  V(NoneLit)             \
  V(UnitLit)             \
  V(VarRef)              \
  V(VariantRef)          \
  V(SeqLit)              \
  V(RecordLit)           \
  V(Block)               \
  V(IfExpr)              \
  V(MatchExpr)           \
  V(MatchArm)            \
  V(ForExpr)             \
  V(Lambda)              \
  V(UiElement)           \
  V(Paren)               \
  V(Unary)               \
  V(Binary)              \
  V(Member)              \
  V(Call)                \
  V(Pipe)                \
  V(WildcardPattern)     \
  V(BindingPattern)      \
  V(NonePattern)         \
  V(VariantPattern)      \
  V(LiteralPattern)
// clang-format on

class Node;
using NodeFn = std::function<void(const Node*)>;

class Node {
 public:
  enum Kind : uint8_t {
#define DEFINE_KIND(Name) k##Name,
    FOR_EACH_NODE(DEFINE_KIND)
#undef DEFINE_KIND
  };

 private:
  Kind kind_;

 protected:
  explicit Node(const Kind kind) :
    kind_(kind) {}

 public:
  Span span{};

  Node(const Node&) = delete;
  Node& operator=(const Node&) = delete;
  virtual ~Node() = default;

  auto GetKind() const -> Kind {
    return kind_;
  }

  auto GetKindName() const -> const char*;

  template <typename T>
  auto Is() const -> bool {
    return kind_ == T::kKind;
  }

  template <typename T>
  auto As() -> T* {
    return Is<T>() ? static_cast<T*>(this) : nullptr;
  }

  template <typename T>
  auto As() const -> const T* {
    return Is<T>() ? static_cast<const T*>(this) : nullptr;
  }

  // Calls `fn` on each direct child, in source order. Never with nullptr.
  virtual void ForEachChild(const NodeFn&) const {}
};

// An identifier as written, with the span of just the identifier. `missing`
// is set when the parser needed a name and there wasn't one; the text is then
// empty and the span is an empty span where the name should have been.
struct Name {
  Span span{};
  std::string text{};
  bool missing = true;

  auto IsRaw() const -> bool {
    return !text.empty() && text.front() == '`';
  }

  // The name without the backticks of a raw name.
  auto GetValue() const -> std::string {
    if (IsRaw() && text.size() >= 2 && text.back() == '`')
      return text.substr(1, text.size() - 2);
    if (IsRaw())
      return text.substr(1);
    return text;
  }
};

#define DEFINE_NODE(Name_)                      \
  static constexpr Kind kKind = k##Name_;       \
  Name_() :                                     \
    Node(kKind) {}

namespace detail {
inline void Visit(const NodeFn& fn, const Node* node) {
  if (node != nullptr)
    fn(node);
}

template <typename T>
void VisitAll(const NodeFn& fn, const std::vector<T*>& nodes) {
  for (const Node* node : nodes)
    Visit(fn, node);
}
}  // namespace detail

// A stand-in for something that did not parse: an item, a type, an expression
// or a pattern. It covers the tokens that were skipped.
struct Error : Node {
  DEFINE_NODE(Error)
};

// ---- declarations -----------------------------------------------------------

// `@name` or `@name(args)`
struct Directive : Node {
  DEFINE_NODE(Directive)
  Name name{};
  bool has_parens = false;
  std::vector<Node*> args{};  // Arg

  void ForEachChild(const NodeFn& fn) const override {
    detail::VisitAll(fn, args);
  }
};

// An argument of a directive or of a `#element(...)`: `name: value`,
// `...value` or just `value`.
struct Arg : Node {
  enum Form : uint8_t { kNamed, kSpread, kPositional };
  DEFINE_NODE(Arg)
  Form form = kPositional;
  Name name{};  // only for kNamed
  Node* value = nullptr;

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, value);
  }
};

// `name: Type` in a function or extern signature.
struct Param : Node {
  DEFINE_NODE(Param)
  Name name{};
  Node* type = nullptr;

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, type);
  }
};

// `name: Type` or `name?: Type` in a record type.
struct Field : Node {
  DEFINE_NODE(Field)
  Name name{};
  bool optional = false;
  Node* type = nullptr;

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, type);
  }
};

struct Module : Node {
  DEFINE_NODE(Module)
  std::vector<Node*> items{};

  void ForEachChild(const NodeFn& fn) const override {
    detail::VisitAll(fn, items);
  }
};

// `type Name<T, U> = ...`
struct TypeDecl : Node {
  DEFINE_NODE(TypeDecl)
  std::vector<Node*> directives{};
  Name name{};
  std::vector<Name> type_params{};
  Node* type = nullptr;

  void ForEachChild(const NodeFn& fn) const override {
    detail::VisitAll(fn, directives);
    detail::Visit(fn, type);
  }
};

// `fn name(params): Result => body`
struct FnDecl : Node {
  DEFINE_NODE(FnDecl)
  std::vector<Node*> directives{};
  Name name{};
  std::vector<Node*> params{};  // Param
  Node* result = nullptr;       // optional
  Node* body = nullptr;

  void ForEachChild(const NodeFn& fn) const override {
    detail::VisitAll(fn, directives);
    detail::VisitAll(fn, params);
    detail::Visit(fn, result);
    detail::Visit(fn, body);
  }
};

// `extern "symbol" fn name(params, ...): Result => body`
struct ExternDecl : Node {
  DEFINE_NODE(ExternDecl)
  std::vector<Node*> directives{};
  bool has_symbol = false;
  Span symbol_span{};
  std::string symbol{};  // unescaped, without quotes
  Name c_name{};
  std::vector<Node*> params{};  // Param
  bool variadic = false;
  Node* result = nullptr;
  Node* body = nullptr;  // optional

  void ForEachChild(const NodeFn& fn) const override {
    detail::VisitAll(fn, directives);
    detail::VisitAll(fn, params);
    detail::Visit(fn, result);
    detail::Visit(fn, body);
  }
};

// `use "header.h" as alias`
struct UseDecl : Node {
  DEFINE_NODE(UseDecl)
  std::vector<Node*> directives{};
  bool has_header = false;
  Span header_span{};
  std::string header{};  // unescaped, without quotes
  bool has_alias = false;
  Name alias{};

  void ForEachChild(const NodeFn& fn) const override {
    detail::VisitAll(fn, directives);
  }
};

// `let name: Type = value` (is_let) or `name = value`. Used both as a
// top-level item and as a statement in a block or a `#element { ... }`.
struct Binding : Node {
  DEFINE_NODE(Binding)
  std::vector<Node*> directives{};
  bool is_let = true;
  Name name{};
  Node* annotation = nullptr;  // optional
  Node* value = nullptr;

  void ForEachChild(const NodeFn& fn) const override {
    detail::VisitAll(fn, directives);
    detail::Visit(fn, annotation);
    detail::Visit(fn, value);
  }
};

// ---- types ------------------------------------------------------------------

// `Name` or `Name<A, B>`
struct NamedType : Node {
  DEFINE_NODE(NamedType)
  Name name{};
  std::vector<Node*> args{};

  void ForEachChild(const NodeFn& fn) const override {
    detail::VisitAll(fn, args);
  }
};

// `Some(T)`: a variant carrying a payload
struct VariantType : Node {
  DEFINE_NODE(VariantType)
  Name name{};
  Node* payload = nullptr;

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, payload);
  }
};

// `alias.Name<A>`: a type that comes from a `use`d header
struct QualifiedType : Node {
  DEFINE_NODE(QualifiedType)
  Name qualifier{};
  Name member{};
  std::vector<Node*> args{};

  void ForEachChild(const NodeFn& fn) const override {
    detail::VisitAll(fn, args);
  }
};

struct NoneType : Node {
  DEFINE_NODE(NoneType)
};

struct RecordType : Node {
  DEFINE_NODE(RecordType)
  std::vector<Node*> fields{};  // Field

  void ForEachChild(const NodeFn& fn) const override {
    detail::VisitAll(fn, fields);
  }
};

// `(A, B) => R`
struct FunctionType : Node {
  DEFINE_NODE(FunctionType)
  std::vector<Node*> params{};
  Node* result = nullptr;

  void ForEachChild(const NodeFn& fn) const override {
    detail::VisitAll(fn, params);
    detail::Visit(fn, result);
  }
};

// `A | B | C`, only built for two or more members
struct UnionType : Node {
  DEFINE_NODE(UnionType)
  std::vector<Node*> members{};

  void ForEachChild(const NodeFn& fn) const override {
    detail::VisitAll(fn, members);
  }
};

// ---- literals ---------------------------------------------------------------

struct NumberLit : Node {
  DEFINE_NODE(NumberLit)
  std::string text{};
};

struct StringLit : Node {
  DEFINE_NODE(StringLit)
  std::string value{};  // unescaped, without quotes
};

struct BoolLit : Node {
  DEFINE_NODE(BoolLit)
  bool value = false;
};

struct NoneLit : Node {
  DEFINE_NODE(NoneLit)
};

// `10px`, `50%`, `1fr`, `red`
struct UnitLit : Node {
  enum Unit : uint8_t { kPx, kPct, kFr, kColor };
  DEFINE_NODE(UnitLit)
  Unit unit = kPx;
  std::string text{};  // as written, with the suffix
};

// ---- expressions ------------------------------------------------------------

// `name`
struct VarRef : Node {
  DEFINE_NODE(VarRef)
  Name name{};
};

// `Name`: a variant constructor used as a value or a callee
struct VariantRef : Node {
  DEFINE_NODE(VariantRef)
  Name name{};
};

struct SeqLit : Node {
  DEFINE_NODE(SeqLit)
  std::vector<Node*> elements{};

  void ForEachChild(const NodeFn& fn) const override {
    detail::VisitAll(fn, elements);
  }
};

// `{ name: value, ...rest }`; the entries are named or spread Args
struct RecordLit : Node {
  DEFINE_NODE(RecordLit)
  std::vector<Node*> entries{};

  void ForEachChild(const NodeFn& fn) const override {
    detail::VisitAll(fn, entries);
  }
};

// `{ let a = 1 a + 1 }`
struct Block : Node {
  DEFINE_NODE(Block)
  std::vector<Node*> statements{};  // Binding
  Node* result = nullptr;           // optional

  void ForEachChild(const NodeFn& fn) const override {
    detail::VisitAll(fn, statements);
    detail::Visit(fn, result);
  }
};

struct IfExpr : Node {
  DEFINE_NODE(IfExpr)
  Node* condition = nullptr;
  Node* then_branch = nullptr;  // Block
  Node* else_branch = nullptr;  // Block or IfExpr

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, condition);
    detail::Visit(fn, then_branch);
    detail::Visit(fn, else_branch);
  }
};

// `pattern => body`
struct MatchArm : Node {
  DEFINE_NODE(MatchArm)
  Node* pattern = nullptr;
  Node* body = nullptr;

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, pattern);
    detail::Visit(fn, body);
  }
};

struct MatchExpr : Node {
  DEFINE_NODE(MatchExpr)
  Node* scrutinee = nullptr;
  std::vector<Node*> arms{};  // MatchArm

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, scrutinee);
    detail::VisitAll(fn, arms);
  }
};

// `for x in xs if guard fold acc = init => body`
struct ForExpr : Node {
  DEFINE_NODE(ForExpr)
  Name binding{};
  Node* iterable = nullptr;
  Node* guard = nullptr;  // optional
  bool has_fold = false;
  Name accumulator{};
  Node* initial = nullptr;
  Node* body = nullptr;

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, iterable);
    detail::Visit(fn, guard);
    detail::Visit(fn, initial);
    detail::Visit(fn, body);
  }
};

// `(a, b) => body`
struct Lambda : Node {
  DEFINE_NODE(Lambda)
  std::vector<Name> params{};
  Node* body = nullptr;

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, body);
  }
};

// `#tag(props) { children }`
struct UiElement : Node {
  DEFINE_NODE(UiElement)
  std::vector<Node*> directives{};
  Name tag{};
  bool has_props = false;
  std::vector<Node*> props{};  // Arg
  bool has_children = false;
  std::vector<Node*> children{};  // UiElement or Binding

  void ForEachChild(const NodeFn& fn) const override {
    detail::VisitAll(fn, directives);
    detail::VisitAll(fn, props);
    detail::VisitAll(fn, children);
  }
};

// `( expr )`, kept so tools can see the parentheses
struct Paren : Node {
  DEFINE_NODE(Paren)
  Node* inner = nullptr;

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, inner);
  }
};

struct Unary : Node {
  enum Op : uint8_t { kNeg, kNot };
  DEFINE_NODE(Unary)
  Op op = kNeg;
  Span op_span{};
  Node* operand = nullptr;

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, operand);
  }
};

struct Binary : Node {
  enum Op : uint8_t { kAdd, kSub, kMul, kDiv, kLt, kLe, kGt, kGe, kEq, kNeq, kAnd, kOr };
  DEFINE_NODE(Binary)
  Op op = kAdd;
  Span op_span{};
  Node* lhs = nullptr;
  Node* rhs = nullptr;

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, lhs);
    detail::Visit(fn, rhs);
  }
};

// `object.member`
struct Member : Node {
  DEFINE_NODE(Member)
  Node* object = nullptr;
  Name member{};

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, object);
  }
};

// `callee(args)`
struct Call : Node {
  DEFINE_NODE(Call)
  Node* callee = nullptr;
  std::vector<Node*> args{};

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, callee);
    detail::VisitAll(fn, args);
  }
};

// `lhs |> callee(args)`: the value on the left becomes the first argument
struct Pipe : Node {
  DEFINE_NODE(Pipe)
  Node* lhs = nullptr;
  Node* callee = nullptr;  // VarRef, VariantRef, Member chain, or Paren
  std::vector<Node*> args{};

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, lhs);
    detail::Visit(fn, callee);
    detail::VisitAll(fn, args);
  }
};

// ---- patterns ---------------------------------------------------------------

struct WildcardPattern : Node {
  DEFINE_NODE(WildcardPattern)
};

struct BindingPattern : Node {
  DEFINE_NODE(BindingPattern)
  Name name{};
};

struct NonePattern : Node {
  DEFINE_NODE(NonePattern)
};

// `Some(x)` or just `Circle`
struct VariantPattern : Node {
  DEFINE_NODE(VariantPattern)
  Name name{};
  Node* payload = nullptr;  // optional

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, payload);
  }
};

// `-1`, `10px`, `"x"`, `true`
struct LiteralPattern : Node {
  DEFINE_NODE(LiteralPattern)
  bool negated = false;
  Node* literal = nullptr;

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, literal);
  }
};

#undef DEFINE_NODE

inline auto Node::GetKindName() const -> const char* {
  switch (kind_) {
#define DEFINE_CASE(Name_) \
  case k##Name_:           \
    return #Name_;
    FOR_EACH_NODE(DEFINE_CASE)
#undef DEFINE_CASE
  }
  return "Unknown";
}

// Owns every node of a tree, so nodes can point at each other freely.
class Arena {
 private:
  std::vector<std::unique_ptr<Node>> nodes_{};

 public:
  Arena() = default;
  ~Arena() = default;
  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;

  template <typename T>
  auto New(const Span& span) -> T* {
    auto node = std::make_unique<T>();
    node->span = span;
    auto* raw = node.get();
    nodes_.push_back(std::move(node));
    return raw;
  }

  auto size() const -> size_t {
    return nodes_.size();
  }
};

// The deepest node whose span touches `offset`; what an editor wants for
// hover, go to definition and completion. Null when the offset is outside
// the root. A tie between a parent and a child goes to the child.
auto FindNodeAt(const Node* root, uint32_t offset) -> const Node*;

// The chain of nodes from `root` down to FindNodeAt(root, offset).
auto FindPathAt(const Node* root, uint32_t offset) -> std::vector<const Node*>;
}  // namespace kura::syntax

#endif  // KURA_SYNTAX_AST_H
