#ifndef KURA_COMPILE_TYPE_H
#define KURA_COMPILE_TYPE_H

#include <string>
#include <vector>

namespace kura {
// The static type of a value in the flow graph.
//
// These are immutable, structural and interned: two types are the same iff they
// are the same pointer, so they compare with `==`. (kura::Type is the runtime
// registry of named types; it can't express records, variants or functions.)
//
//   Any      unknown: operations on it are checked at runtime. A lambda
//            parameter with no context to infer it from is Any.
//   Never    no value (an unreachable result); the identity of Join.
//   Number   an Int or a Float whose kind isn't known statically. It's what a
//            `Number` annotation means.
//   Variant  one constructor of a sum type: `Click(Position)`, `Tick`.
//   Union    `Node | None`, `Click(Position) | Tick`.
//   Handle   an opaque pointer that came from C (`FILE*`, `Texture*`). It can be
//            held and handed back to C, and compared, but not looked into.
class CompileType {
 public:
  enum Kind {
    kAny,
    kNever,
    kNone,
    kBool,
    kInt,
    kFloat,
    kNumber,
    kString,
    kColor,
    kPx,
    kPct,
    kFr,
    kNode,
    kSeq,
    kRecord,
    kVariant,
    kFunction,
    kUnion,
    kHandle,
  };

  struct Field {
    std::string name;
    CompileType* type;
    bool optional;  // `z?: T`: may be absent; reads give `T | None`
  };

 private:
  Kind kind_;
  std::string repr_{};
  CompileType* element_ = nullptr;     // Seq
  std::vector<Field> fields_{};        // Record, sorted by name
  std::string name_{};                 // Variant
  CompileType* payload_ = nullptr;     // Variant (null: no payload)
  std::vector<CompileType*> params_{}; // Function
  CompileType* result_ = nullptr;      // Function
  std::vector<CompileType*> members_{};  // Union, sorted, flattened

  explicit CompileType(const Kind kind) :
    kind_(kind) {}

  static auto Intern(CompileType* proto) -> CompileType*;

 public:
  ~CompileType() = default;

  auto GetKind() const -> Kind {
    return kind_;
  }

  // `Seq<Int>`, `{ x: Int, y?: Int }`, `(Int) => Int`, `Node | None`
  auto ToString() const -> const std::string& {
    return repr_;
  }

#define DEFINE_KIND_CHECK(Name)            \
  inline auto Is##Name() const -> bool {   \
    return kind_ == Kind::k##Name;         \
  }
  DEFINE_KIND_CHECK(Any)
  DEFINE_KIND_CHECK(Never)
  DEFINE_KIND_CHECK(None)
  DEFINE_KIND_CHECK(Bool)
  DEFINE_KIND_CHECK(Int)
  DEFINE_KIND_CHECK(Float)
  DEFINE_KIND_CHECK(Number)
  DEFINE_KIND_CHECK(String)
  DEFINE_KIND_CHECK(Color)
  DEFINE_KIND_CHECK(Px)
  DEFINE_KIND_CHECK(Pct)
  DEFINE_KIND_CHECK(Fr)
  DEFINE_KIND_CHECK(Node)
  DEFINE_KIND_CHECK(Seq)
  DEFINE_KIND_CHECK(Record)
  DEFINE_KIND_CHECK(Variant)
  DEFINE_KIND_CHECK(Function)
  DEFINE_KIND_CHECK(Union)
  DEFINE_KIND_CHECK(Handle)
#undef DEFINE_KIND_CHECK

  // Int, Float, or Number.
  inline auto IsNumeric() const -> bool {
    return IsInt() || IsFloat() || IsNumber();
  }

  // Px, Pct, or Fr.
  inline auto IsUnit() const -> bool {
    return IsPx() || IsPct() || IsFr();
  }

  auto GetElementType() const -> CompileType* {
    return element_;
  }

  auto GetFields() const -> const std::vector<Field>& {
    return fields_;
  }

  auto FindField(const std::string& name) const -> const Field*;

  auto GetVariantName() const -> const std::string& {
    return name_;
  }

  auto GetHandleName() const -> const std::string& {
    return name_;
  }

  auto GetPayload() const -> CompileType* {
    return payload_;
  }

  auto GetParameters() const -> const std::vector<CompileType*>& {
    return params_;
  }

  auto GetResult() const -> CompileType* {
    return result_;
  }

  auto GetMembers() const -> const std::vector<CompileType*>& {
    return members_;
  }

  // The members of a union, or the type itself.
  auto GetAlternatives() const -> std::vector<CompileType*>;

  // Does the type include None?
  auto IsNullable() const -> bool;

  // `Node | None` -> `Node`
  auto WithoutNone() const -> CompileType*;

  // The variant called `name` among the alternatives, if there is one.
  auto FindVariant(const std::string& name) const -> CompileType*;

  // Can a value of type `from` be used where `to` is expected?
  static auto IsAssignable(CompileType* from, CompileType* to) -> bool;

  // The least type that both `a` and `b` are assignable to.
  static auto Join(CompileType* a, CompileType* b) -> CompileType*;

 public:
  static auto Any() -> CompileType*;
  static auto Never() -> CompileType*;
  static auto None() -> CompileType*;
  static auto Bool() -> CompileType*;
  static auto Int() -> CompileType*;
  static auto Float() -> CompileType*;
  static auto Number() -> CompileType*;
  static auto String() -> CompileType*;
  static auto Color() -> CompileType*;
  static auto Px() -> CompileType*;
  static auto Pct() -> CompileType*;
  static auto Fr() -> CompileType*;
  static auto Node() -> CompileType*;
  static auto Seq(CompileType* element) -> CompileType*;
  static auto Handle(const std::string& name) -> CompileType*;
  static auto Record(std::vector<Field> fields) -> CompileType*;
  static auto Variant(const std::string& name, CompileType* payload = nullptr) -> CompileType*;
  static auto Function(std::vector<CompileType*> params, CompileType* result) -> CompileType*;
  static auto Union(std::vector<CompileType*> members) -> CompileType*;
};
}  // namespace kura

#endif  // KURA_COMPILE_TYPE_H
