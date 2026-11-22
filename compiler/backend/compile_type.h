#ifndef KURA_COMPILE_TYPE_H
#define KURA_COMPILE_TYPE_H

#include <optional>
#include <string>
#include <vector>

#include "heap/color_model.h"

namespace kura {
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
    kVec2,
    kVec3,
    kRect,
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

#define DEFINE_KIND(Name) k##Name,
    FOR_EACH_COLOR_MODEL(DEFINE_KIND)
#undef DEFINE_KIND
  };

  struct Field {
    std::string name;
    CompileType* type;
    bool optional;  // `z?: T`: may be absent; reads give `T | None`
  };

 private:
  Kind kind_;
  std::string repr_{};
  CompileType* element_ = nullptr;       // Seq
  std::vector<Field> fields_{};          // Record, sorted by name
  std::string name_{};                   // Variant
  CompileType* payload_ = nullptr;       // Variant (null: no payload)
  std::vector<CompileType*> params_{};   // Function
  CompileType* result_ = nullptr;        // Function
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

#define DEFINE_KIND_CHECK(Name)          \
  inline auto Is##Name() const -> bool { \
    return kind_ == Kind::k##Name;       \
  }
  DEFINE_KIND_CHECK(Any)
  DEFINE_KIND_CHECK(Never)
  DEFINE_KIND_CHECK(None)
  DEFINE_KIND_CHECK(Bool)
  DEFINE_KIND_CHECK(Int)
  DEFINE_KIND_CHECK(Float)
  DEFINE_KIND_CHECK(Number)
  DEFINE_KIND_CHECK(String)
  DEFINE_KIND_CHECK(Vec2)
  DEFINE_KIND_CHECK(Vec3)
  DEFINE_KIND_CHECK(Rect)
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
  FOR_EACH_COLOR_MODEL(DEFINE_KIND_CHECK)
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

  auto IsColor() const -> bool {
    switch (kind_) {
#define DEFINE_CASE(Name) \
  case Kind::k##Name:     \
    return true;
      FOR_EACH_COLOR_MODEL(DEFINE_CASE)
#undef DEFINE_CASE
      default:
        return false;
    }
  }

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

#define DEFINE_NEW(Name) static auto Name() -> CompileType*;
  FOR_EACH_COLOR_MODEL(DEFINE_NEW)
#undef DEFINE_NEW

  static auto Color(ColorModel model = ColorModel::kRgbaColor) -> CompileType*;
  // Geometry: the compiler holds these unboxed, as vectors of floats, and they have no tagged form (see geometry.h).
  static auto Vec2() -> CompileType*;
  static auto Vec3() -> CompileType*;
  static auto Rect() -> CompileType*;
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

static inline auto VectorPropertyLane(CompileType* obj_ty, std::string_view prop) -> std::optional<uint32_t> {
  if (obj_ty->IsRgbaColor() || obj_ty->IsSrgbaColor()) {
    if (prop == "r" || prop == "red")
      return 0;
    if (prop == "g" || prop == "green")
      return 1;
    if (prop == "b" || prop == "blue")
      return 2;
    if (prop == "a" || prop == "alpha")
      return 3;
    return std::nullopt;
  }

  if (obj_ty->IsHslaColor()) {
    if (prop == "h" || prop == "hue")
      return 0;
    if (prop == "s" || prop == "saturation")
      return 1;
    if (prop == "l" || prop == "lightness")
      return 2;
    if (prop == "a" || prop == "alpha")
      return 3;
    return std::nullopt;
  }

  if (obj_ty->IsHsbaColor()) {
    if (prop == "h" || prop == "hue")
      return 0;
    if (prop == "s" || prop == "saturation")
      return 1;
    if (prop == "v" || prop == "value" || prop == "b" || prop == "brightness")
      return 2;
    if (prop == "a" || prop == "alpha")
      return 3;
    return std::nullopt;
  }

  if (obj_ty->IsVec2()) {
    if (prop == "x")
      return 0;
    if (prop == "y")
      return 1;
    return std::nullopt;
  }

  if (obj_ty->IsVec3()) {
    if (prop == "x")
      return 0;
    if (prop == "y")
      return 1;
    if (prop == "z")
      return 2;
    return std::nullopt;
  }

  if (obj_ty->IsRect()) {
    if (prop == "x")
      return 0;
    if (prop == "y")
      return 1;
    if (prop == "w" || prop == "width")
      return 2;
    if (prop == "h" || prop == "height")
      return 3;
    return std::nullopt;
  }

  return std::nullopt;
}

static inline auto GetColorModel(const CompileType& rhs) -> std::optional<ColorModel> {
  switch (rhs.GetKind()) {
#define DEFINE_CASE(Name)    \
  case CompileType::k##Name: \
    return {ColorModel::k##Name};
    FOR_EACH_COLOR_MODEL(DEFINE_CASE)
#undef DEFINE_CASE
    default:
      return std::nullopt;
  }
}
}  // namespace kura

#endif  // KURA_COMPILE_TYPE_H
