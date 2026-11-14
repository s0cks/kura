#ifndef KURA_SYNTAX_AST_H
#define KURA_SYNTAX_AST_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "compiler/frontend/syntax/diagnostic.h"
#include "compiler/frontend/syntax/source.h"
#include "compiler/frontend/syntax/token.h"

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
  V(LiteralPattern)      \
  V(Placeholder)         \
  V(StringTemplate)      \
  V(RangeExpr)           \
  V(IsExpr)              \
  V(WhenExpr)            \
  V(Index)               \
  V(PipeBack)            \
  V(RecordUpdate)        \
  V(UpdateEntry)         \
  V(SetRule)             \
  V(StaticAssert)        \
  V(RecordPattern)       \
  V(FieldPattern)        \
  V(RangePattern)
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

#ifdef KURA_ENABLE_HEAP

#define DEFINE_NODE(Name_)                            \
  static constexpr Kind kKind = k##Name_;             \
  Name_() :                                           \
    Node(kKind) {}                                    \
  auto operator new(const size_t sz)->void*;          \
  void operator delete(void* ptr) {}                  \
  static inline auto New(const Span span) -> Name_* { \
    Name_* value = new Name_();                       \
    value->span = std::move(span);                    \
    return value;                                     \
  }

#else

#define DEFINE_NODE(Name_)                            \
  static constexpr Kind kKind = k##Name_;             \
  Name_() :                                           \
    Node(kKind) {}                                    \
  static inline auto New(const Span span) -> Name_* { \
    Name_* value = new Name_();                       \
    value->span = std::move(span);                    \
    return value;                                     \
  }

#endif  // KURA_ENABLE_HEAP

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

// An argument of a directive or of a `#element(...)`, or an entry of a record
// literal: `name: value`, `...value`, just `value`, or a pun, `name,`, which
// stands for `name: name` (the value is then a VarRef the parser made up).
struct Arg : Node {
  enum Form : uint8_t { kNamed, kSpread, kPositional, kPun };
  DEFINE_NODE(Arg)
  Form form = kPositional;
  Name name{};  // for kNamed and kPun
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

// `fn name(params): Result => set rules... body`
//
// A body written without braces may start with any number of `set` rules and
// ends with one expression; a `let` needs a braced body.
struct FnDecl : Node {
  DEFINE_NODE(FnDecl)
  std::vector<Node*> directives{};
  Name name{};
  std::vector<Node*> params{};     // Param
  Node* result = nullptr;          // optional
  std::vector<Node*> set_rules{};  // SetRule, before an unbraced body
  Node* body = nullptr;

  void ForEachChild(const NodeFn& fn) const override {
    detail::VisitAll(fn, directives);
    detail::VisitAll(fn, params);
    detail::Visit(fn, result);
    detail::VisitAll(fn, set_rules);
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
// `let { a, b: c, ...rest } = value` destructures: `pattern` is then a
// RecordPattern and `name` is missing.
struct Binding : Node {
  DEFINE_NODE(Binding)
  std::vector<Node*> directives{};
  bool is_let = true;
  Name name{};
  Node* pattern = nullptr;     // optional, instead of `name`
  Node* annotation = nullptr;  // optional
  Node* value = nullptr;

  void ForEachChild(const NodeFn& fn) const override {
    detail::VisitAll(fn, directives);
    detail::Visit(fn, pattern);
    detail::Visit(fn, annotation);
    detail::Visit(fn, value);
  }
};

// `set button(fill: black)`: default properties for the elements after it in
// the enclosing scope. Allowed as an item, in a block, in the children of an
// element and at the start of an unbraced function body.
struct SetRule : Node {
  DEFINE_NODE(SetRule)
  Name tag{};
  bool has_props = false;
  std::vector<Node*> props{};  // Arg

  void ForEachChild(const NodeFn& fn) const override {
    detail::VisitAll(fn, props);
  }
};

// `static-assert(condition, "message")`, checked at compile time.
struct StaticAssert : Node {
  DEFINE_NODE(StaticAssert)
  Node* condition = nullptr;
  Node* message = nullptr;  // optional; a StringLit or StringTemplate

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, condition);
    detail::Visit(fn, message);
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

// `"x is {pos.x}"`: text with expressions in `{...}`. There is always one more
// piece of text than there are expressions (the text may be empty):
// texts[0] exprs[0] texts[1] ... exprs[n-1] texts[n].
struct StringTemplate : Node {
  DEFINE_NODE(StringTemplate)
  std::vector<std::string> texts{};  // unescaped
  std::vector<Node*> exprs{};

  void ForEachChild(const NodeFn& fn) const override {
    detail::VisitAll(fn, exprs);
  }
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

// One entry of a record update: `x: value`, `x += value` (also `-=` `*=` `/=`
// `%=`), with a nested path like `pos.x` allowed on the left.
struct UpdateEntry : Node {
  enum Op : uint8_t { kSet, kAdd, kSub, kMul, kDiv, kMod };
  DEFINE_NODE(UpdateEntry)
  std::vector<Name> path{};
  Op op = kSet;
  Span op_span{};
  Node* value = nullptr;

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, value);
  }
};

// `{ base | x: 1, y += 2 }`: a copy of `base` with some fields changed. Only
// the listed fields change, which is what tells the compiler exactly which
// parts of the model an `update` touched.
struct RecordUpdate : Node {
  DEFINE_NODE(RecordUpdate)
  Node* base = nullptr;
  std::vector<Node*> entries{};  // UpdateEntry

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, base);
    detail::VisitAll(fn, entries);
  }
};

// `{ let a = 1 a + 1 }`
struct Block : Node {
  DEFINE_NODE(Block)
  std::vector<Node*> statements{};  // Binding, SetRule or StaticAssert
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

// `when condition { body }`: the body's value if the condition holds, and
// None if it doesn't (an `if` whose `else` is implicitly None).
struct WhenExpr : Node {
  DEFINE_NODE(WhenExpr)
  Node* condition = nullptr;
  Node* body = nullptr;  // Block

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, condition);
    detail::Visit(fn, body);
  }
};

// `pattern => body`, or `pattern if guard => body`
struct MatchArm : Node {
  DEFINE_NODE(MatchArm)
  Node* pattern = nullptr;
  Node* guard = nullptr;  // optional
  Node* body = nullptr;

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, pattern);
    detail::Visit(fn, guard);
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

// `for x in xs if guard fold acc = init => body`, or in brackets, as a
// comprehension: `[ body for x in xs if guard ]`.
//
// The bindings follow Go: with one name, `for i in xs`, the name is the
// INDEX; with two, `for i, x in xs`, they are the index and the element. A `_`
// skips one (`for _, x in xs`). The element may be a pattern, `for _, { a, b } in xs`.
struct ForExpr : Node {
  DEFINE_NODE(ForExpr)
  Name first{};            // the index; `_` when it is skipped
  Node* second = nullptr;  // optional: a pattern for the element
  bool bracketed = false;  // `[ body for ... ]`; has no fold
  Node* iterable = nullptr;
  Node* guard = nullptr;  // optional
  bool has_fold = false;
  Name accumulator{};
  Node* initial = nullptr;
  Node* body = nullptr;

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, second);
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
  // UiElement, Binding or SetRule, and expressions that stand for content: a
  // string (a text node), a `10px`/`50%`/`1fr` spacer, `when`, `if`, `for`,
  // `match`, or a call to a helper that returns a node.
  std::vector<Node*> children{};

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
  // `??` is the default for a None; `>>` and `<<` compose functions.
  enum Op : uint8_t {
    kAdd,
    kSub,
    kMul,
    kDiv,
    kLt,
    kLe,
    kGt,
    kGe,
    kEq,
    kNeq,
    kAnd,
    kOr,
    kMod,
    kCoalesce,
    kCompose,
    kComposeBack
  };
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

// `object.member`, or `object?.member` when `optional` (None if `object` is)
struct Member : Node {
  DEFINE_NODE(Member)
  Node* object = nullptr;
  Name member{};
  bool optional = false;

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, object);
  }
};

// `callee(args)`, or `callee?.(args)` when `optional`. An argument may be a
// Placeholder: `add(_, 10)` is a function still waiting for its first argument.
struct Call : Node {
  DEFINE_NODE(Call)
  Node* callee = nullptr;
  bool optional = false;
  std::vector<Node*> args{};

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, callee);
    detail::VisitAll(fn, args);
  }
};

// `lhs |> stage`. Two forms:
//  - a call without any `_`, `x |> f(y)`: the value becomes the first
//    argument, so it is `f(x, y)`. `callee` and `args` hold the call.
//  - a stage with `_` in it, `x |> add(_, 10)` or `x |> _ + 1`: every `_` is the
//    value. `stage` holds the whole expression, and `callee` is null.
struct Pipe : Node {
  DEFINE_NODE(Pipe)
  Node* lhs = nullptr;
  Node* callee = nullptr;  // VarRef, VariantRef, Member chain, or Paren
  std::vector<Node*> args{};
  Node* stage = nullptr;  // set instead of callee/args when the stage uses `_`

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, lhs);
    detail::Visit(fn, callee);
    detail::VisitAll(fn, args);
    detail::Visit(fn, stage);
  }
};

// `f <| x`: apply the function on the left to the value on the right.
// Right associative, so `f <| g <| x` is `f(g(x))`.
struct PipeBack : Node {
  DEFINE_NODE(PipeBack)
  Node* function = nullptr;
  Node* argument = nullptr;

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, function);
    detail::Visit(fn, argument);
  }
};

// `_` in an expression: the value of the enclosing `|>`, or the missing
// argument of a partial application.
struct Placeholder : Node {
  DEFINE_NODE(Placeholder)
};

// `a..b`, `a..=b`, `a..`, `..b`: a range of integers. `..` leaves out the
// end, `..=` includes it; a missing side is open.
struct RangeExpr : Node {
  DEFINE_NODE(RangeExpr)
  Node* begin = nullptr;  // optional
  Node* end = nullptr;    // optional
  bool inclusive = false;

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, begin);
    detail::Visit(fn, end);
  }
};

// `value is pattern`: whether the value has that shape, and (in an `if`) a
// way to use what it matched.
struct IsExpr : Node {
  DEFINE_NODE(IsExpr)
  Node* value = nullptr;
  Node* pattern = nullptr;

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, value);
    detail::Visit(fn, pattern);
  }
};

// `object[index]`, or `object?.[index]` when `optional`. The index may be a
// RangeExpr, which makes it a slice: `s[1..3]`, `s[0..-1]`.
struct Index : Node {
  DEFINE_NODE(Index)
  Node* object = nullptr;
  Node* index = nullptr;
  bool optional = false;

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, object);
    detail::Visit(fn, index);
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

// `Point { x, y: 0, ...rest }` or `{ x, y }`: a record with some of its fields
// matched. A field with no `:` binds a name of its own name (a pun).
struct RecordPattern : Node {
  DEFINE_NODE(RecordPattern)
  bool has_type = false;
  Name type_name{};
  std::vector<Node*> fields{};  // FieldPattern
  bool has_rest = false;        // `...rest`, or a bare `...`
  Name rest{};                  // missing for a bare `...`

  void ForEachChild(const NodeFn& fn) const override {
    detail::VisitAll(fn, fields);
  }
};

// `name` or `name: pattern` inside a RecordPattern
struct FieldPattern : Node {
  DEFINE_NODE(FieldPattern)
  Name name{};
  Node* pattern = nullptr;  // null for a pun

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, pattern);
  }
};

// `0..10`, `0..=9`, `..0`, `100..`: a range of numbers or units; each end is a
// LiteralPattern.
struct RangePattern : Node {
  DEFINE_NODE(RangePattern)
  Node* begin = nullptr;  // optional
  Node* end = nullptr;    // optional
  bool inclusive = false;

  void ForEachChild(const NodeFn& fn) const override {
    detail::Visit(fn, begin);
    detail::Visit(fn, end);
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

// The deepest node whose span touches `offset`; what an editor wants for
// hover, go to definition and completion. Null when the offset is outside
// the root. A tie between a parent and a child goes to the child.
auto FindNodeAt(const Node* root, uint32_t offset) -> const Node*;

// The chain of nodes from `root` down to FindNodeAt(root, offset).
auto FindPathAt(const Node* root, uint32_t offset) -> std::vector<const Node*>;
}  // namespace kura::syntax

#endif  // KURA_SYNTAX_AST_H
