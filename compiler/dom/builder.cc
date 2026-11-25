#include "dom/builder.h"

#include <algorithm>

#include "backend/swizzle.h"
#include "color.h"
#include "frontend/expr.h"
#include "function.h"
#include "heap/color_model.h"
#include "heap/color_storage.h"
#include "local_scope.h"
#include "object.h"

namespace kura::dom {
namespace {
auto ToDomKind(const expr::NodeExpr::Kind kind) -> Kind {
  switch (kind) {
#define DEFINE_CASE(Name, Tag)        \
  case expr::NodeExpr::Kind::k##Name: \
    return Kind::k##Name;
    FOR_EACH_ELEMENT_NODE(DEFINE_CASE)
#undef DEFINE_CASE
  }
  return Kind::kFragment;
}

template <typename T>
inline auto ValueOf(Object* object) -> decltype(static_cast<T*>(object)->GetValue()) {
  return static_cast<T*>(object)->GetValue();
}

auto FoldUnary(const UnaryOp op, Object* value) -> Object* {
  if (!value)
    return nullptr;

  // a string made of parts is made when the view runs, unless it is only a string
  if (op == UnaryOp::kStringify)
    return value->GetType() == kStringType ? value : nullptr;

  if (op == UnaryOp::kBang)
    return value->GetType() == kBoolType ? Bool::New(!ValueOf<Bool>(value)) : nullptr;

  const auto negate = op == UnaryOp::kMinus;
  switch (value->GetType()) {
    case kIntType:
      if (negate)
        return ValueOf<Int>(value) == std::numeric_limits<int64_t>::min() ? nullptr : Int::New(-ValueOf<Int>(value));
      return value;
    case kFloatType:
      return negate ? Float::New(-ValueOf<Float>(value)) : value;
    case kNumberType:
      return negate ? Number::New(-ValueOf<Number>(value)) : value;
    case kPxType:
      return negate ? Px::New(-ValueOf<Px>(value)) : value;
    case kPctType:
      return negate ? Pct::New(-ValueOf<Pct>(value)) : value;
    case kFrType:
      return negate ? Fr::New(-ValueOf<Fr>(value)) : value;
    default:
      return nullptr;
  }
}

auto IsUnitObject(Object* value) -> bool {
  const auto type = value->GetType();
  return type == kPxType || type == kPctType || type == kFrType;
}

auto UnitValue(Object* value) -> double {
  switch (value->GetType()) {
    case kPxType:
      return ValueOf<Px>(value);
    case kPctType:
      return ValueOf<Pct>(value);
    default:
      return ValueOf<Fr>(value);
  }
}

auto MakeUnit(Object* like, const double value) -> Object* {
  switch (like->GetType()) {
    case kPxType:
      return Px::New(value);
    case kPctType:
      return Pct::New(value);
    default:
      return Fr::New(value);
  }
}

auto NumberValue(Object* value, double* out) -> bool {
  switch (value->GetType()) {
    case kIntType:
      *out = static_cast<double>(ValueOf<Int>(value));
      return true;
    case kFloatType:
      *out = ValueOf<Float>(value);
      return true;
    case kNumberType:
      *out = ValueOf<Number>(value);
      return true;
    default:
      return false;
  }
}

auto FoldMathCall(const MathOp op, Object* a, Object* b, Object* c) -> Object* {
  if (!a)
    return nullptr;

  if (op == MathOp::kLength || op == MathOp::kNormalize) {
    if (const auto* v2 = dynamic_cast<Vec2*>(a)) {
      const auto& v = v2->GetValue();
      const float len = std::hypot(v.x, v.y);
      if (op == MathOp::kLength)
        return Float::New(len);
      return len > 0.0f ? Vec2::New(v.x / len, v.y / len) : Vec2::New(0.0f, 0.0f);
    }
    if (const auto* v3 = dynamic_cast<Vec3*>(a)) {
      const auto& v = v3->GetValue();
      const float len = std::hypot(v.x, v.y, v.z);
      if (op == MathOp::kLength)
        return Float::New(len);
      return len > 0.0f ? Vec3::New(v.x / len, v.y / len, v.z / len) : Vec3::New(0.0f, 0.0f, 0.0f);
    }
    return nullptr;
  }

  if (!b)
    return nullptr;

  if (op == MathOp::kDot) {
    if (const auto* va = dynamic_cast<Vec2*>(a)) {
      if (const auto* vb = dynamic_cast<Vec2*>(b))
        return Float::New(va->GetValue().x * vb->GetValue().x + va->GetValue().y * vb->GetValue().y);
    }
    if (const auto* va = dynamic_cast<Vec3*>(a)) {
      if (const auto* vb = dynamic_cast<Vec3*>(b))
        return Float::New(va->GetValue().x * vb->GetValue().x + va->GetValue().y * vb->GetValue().y +
                          va->GetValue().z * vb->GetValue().z);
    }
    return nullptr;
  }

  if (!c)
    return nullptr;

  if (op == MathOp::kClamp) {
    if (a->GetType() == kIntType && b->GetType() == kIntType && c->GetType() == kIntType)
      return Int::New(std::clamp(ValueOf<Int>(a), ValueOf<Int>(b), ValueOf<Int>(c)));
    if (a->GetType() == kFloatType && b->GetType() == kFloatType && c->GetType() == kFloatType)
      return Float::New(std::clamp(ValueOf<Float>(a), ValueOf<Float>(b), ValueOf<Float>(c)));
    if (a->GetType() == kPxType && b->GetType() == kPxType && c->GetType() == kPxType)
      return Px::New(std::clamp(ValueOf<Px>(a), ValueOf<Px>(b), ValueOf<Px>(c)));
    if (a->GetType() == kPctType && b->GetType() == kPctType && c->GetType() == kPctType)
      return Pct::New(std::clamp(ValueOf<Pct>(a), ValueOf<Pct>(b), ValueOf<Pct>(c)));
    if (a->GetType() == kFrType && b->GetType() == kFrType && c->GetType() == kFrType)
      return Fr::New(std::clamp(ValueOf<Fr>(a), ValueOf<Fr>(b), ValueOf<Fr>(c)));
    return nullptr;
  }

  if (op == MathOp::kLerp) {
    double t = 0.0;
    if (!NumberValue(c, &t))
      return nullptr;
    const float tf = static_cast<float>(t);

    if (a->GetType() == kFloatType && b->GetType() == kFloatType)
      return Float::New(std::lerp(ValueOf<Float>(a), ValueOf<Float>(b), t));
    if (a->GetType() == kPxType && b->GetType() == kPxType)
      return Px::New(std::lerp(ValueOf<Px>(a), ValueOf<Px>(b), t));
    if (a->GetType() == kPctType && b->GetType() == kPctType)
      return Pct::New(std::lerp(ValueOf<Pct>(a), ValueOf<Pct>(b), t));
    if (a->GetType() == kFrType && b->GetType() == kFrType)
      return Fr::New(std::lerp(ValueOf<Fr>(a), ValueOf<Fr>(b), t));

    if (const auto* va = dynamic_cast<Vec2*>(a)) {
      if (const auto* vb = dynamic_cast<Vec2*>(b))
        return Vec2::New(std::lerp(va->GetValue().x, vb->GetValue().x, tf),
                         std::lerp(va->GetValue().y, vb->GetValue().y, tf));
    }
    if (const auto* va = dynamic_cast<Vec3*>(a)) {
      if (const auto* vb = dynamic_cast<Vec3*>(b))
        return Vec3::New(std::lerp(va->GetValue().x, vb->GetValue().x, tf),
                         std::lerp(va->GetValue().y, vb->GetValue().y, tf),
                         std::lerp(va->GetValue().z, vb->GetValue().z, tf));
    }
    if (const auto* ra = dynamic_cast<Rect*>(a)) {
      if (const auto* rb = dynamic_cast<Rect*>(b))
        return Rect::New(std::lerp(ra->GetValue().x, rb->GetValue().x, tf),
                         std::lerp(ra->GetValue().y, rb->GetValue().y, tf),
                         std::lerp(ra->GetValue().width, rb->GetValue().width, tf),
                         std::lerp(ra->GetValue().height, rb->GetValue().height, tf));
    }

#define DEFINE_COLOR_LERP(Name)                                                                                     \
  if (a->GetType() == k##Name##Type && b->GetType() == k##Name##Type) {                                             \
    const auto& sa = static_cast<Name*>(a)->GetStorage();                                                           \
    const auto& sb = static_cast<Name*>(b)->GetStorage();                                                           \
    float c0 = std::lerp(sa.c0, sb.c0, tf);                                                                         \
    if constexpr (ColorModel::k##Name == ColorModel::kHslaColor || ColorModel::k##Name == ColorModel::kHsbaColor) { \
      const float dh = std::fmod(sb.c0 - sa.c0 + 540.0f, 360.0f) - 180.0f;                                          \
      c0 = std::fmod(sa.c0 + dh * tf + 360.0f, 360.0f);                                                             \
    }                                                                                                               \
    return Name::New({c0, std::lerp(sa.c1, sb.c1, tf), std::lerp(sa.c2, sb.c2, tf), std::lerp(sa.a, sb.a, tf)});    \
  }
    FOR_EACH_COLOR_MODEL(DEFINE_COLOR_LERP)
#undef DEFINE_COLOR_LERP
  }

  return nullptr;
}

auto FoldBinary(const BinaryOp op, Object* lhs, Object* rhs) -> Object* {
  if (!lhs || !rhs)
    return nullptr;

  // a unit scaled by a number: `2 * 8px`, `8px * 2`, `8px / 2`
  if (lhs->GetType() != rhs->GetType()) {
    double n = 0;
    if (IsUnitObject(lhs) && NumberValue(rhs, &n) && op == BinaryOp::kMultiply)
      return MakeUnit(lhs, UnitValue(lhs) * n);
    if (IsUnitObject(lhs) && NumberValue(rhs, &n) && op == BinaryOp::kDivide && n != 0)
      return MakeUnit(lhs, UnitValue(lhs) / n);
    if (NumberValue(lhs, &n) && IsUnitObject(rhs) && op == BinaryOp::kMultiply)
      return MakeUnit(rhs, n * UnitValue(rhs));
    return nullptr;
  }

  switch (lhs->GetType()) {
    case kIntType: {
      const auto x = ValueOf<Int>(lhs);
      const auto y = ValueOf<Int>(rhs);
      int64_t result = 0;
      switch (op) {
        case BinaryOp::kAdd:
          return __builtin_add_overflow(x, y, &result) ? nullptr : Int::New(result);
        case BinaryOp::kSubtract:
          return __builtin_sub_overflow(x, y, &result) ? nullptr : Int::New(result);
        case BinaryOp::kMultiply:
          return __builtin_mul_overflow(x, y, &result) ? nullptr : Int::New(result);
        default:
          return nullptr;
      }
    }
    case kFloatType:
    case kNumberType: {
      const auto x = lhs->GetType() == kFloatType ? ValueOf<Float>(lhs) : ValueOf<Number>(lhs);
      const auto y = rhs->GetType() == kFloatType ? ValueOf<Float>(rhs) : ValueOf<Number>(rhs);
      double result = 0;
      switch (op) {
        case BinaryOp::kAdd:
          result = x + y;
          break;
        case BinaryOp::kSubtract:
          result = x - y;
          break;
        case BinaryOp::kMultiply:
          result = x * y;
          break;
        case BinaryOp::kDivide:
          result = x / y;
          break;
        default:
          return nullptr;
      }
      return lhs->GetType() == kFloatType ? static_cast<Object*>(Float::New(result)) : Number::New(result);
    }
    case kPxType:
    case kPctType:
    case kFrType: {
      if (op != BinaryOp::kAdd && op != BinaryOp::kSubtract)
        return nullptr;
      const auto sign = op == BinaryOp::kAdd ? 1.0 : -1.0;
      if (lhs->GetType() == kPxType)
        return Px::New(ValueOf<Px>(lhs) + sign * ValueOf<Px>(rhs));
      if (lhs->GetType() == kPctType)
        return Pct::New(ValueOf<Pct>(lhs) + sign * ValueOf<Pct>(rhs));
      return Fr::New(ValueOf<Fr>(lhs) + sign * ValueOf<Fr>(rhs));
    }
    default:
      return nullptr;
  }
}

void AddTag(TagList* tags, const std::string& tag) {
  if (std::find(tags->begin(), tags->end(), tag) == tags->end())
    tags->push_back(tag);
}

void CollectTags(expr::Expr* expr, TagList* tags) {
  if (!expr)
    return;
  if (const auto list = expr->AsList()) {
    for (const auto child : list->GetChildren())
      CollectTags(child, tags);
  } else if (const auto variant = expr->AsVariant()) {
    AddTag(tags, variant->GetVariantName());
  }
}

void CollectMetaTags(const expr::DirectiveList& directives, TagList* tags) {
  for (const auto directive : directives) {
    if (directive->name != "meta")
      continue;
    for (const auto arg : directive->args) {
      if (arg->GetProperty()->GetPropertyName()->GetValue() == "tags")
        CollectTags(arg->GetValue(), tags);
    }
  }
}

// Whether a function returns a node. A declared result of `Node`, `Content`, `Optional<Node>` or `Node | None`
// says so. Without one, a body that ends in a UI element does.
auto ReturnsNode(Function* function) -> bool {
  if (const auto type = function->GetReturnType()) {
    std::string name = type->ToString();
    name.erase(std::remove(name.begin(), name.end(), ' '), name.end());
    return name == "Node" || name == "Content" || name == "Optional<Node>" || name == "Node|None" ||
           name == "None|Node";
  }

  const auto body = function->GetBody();
  const auto count = body ? body->GetNumberOfChildren() : 0;
  return count > 0 && body->GetChildAt(count - 1)->AsNode() != nullptr;
}
}  // namespace

void Builder::Error(const std::string& message) {
  errors_.push_back(message);
}

auto MakeColorObject(const ColorModel model, const ColorStorage& storage) -> Object* {
  switch (model) {
#define DEFINE_CASE(Name)   \
  case ColorModel::k##Name: \
    return Name::New(storage);
    FOR_EACH_COLOR_MODEL(DEFINE_CASE)
#undef DEFINE_CASE
    default:
      return nullptr;
  }
}

auto NormalizeColorChannel(const ColorModel model, const int lane, Object* obj) -> std::optional<float> {
  if (!obj)
    return std::nullopt;

  // Percentage (e.g., 80% -> 0.8f) works on every channel except Hue
  if (obj->GetType() == kPctType && !(lane == 0 && model != ColorModel::kRgbaColor))
    return static_cast<float>(ValueOf<Pct>(obj) / 100.0);

  double val = 0.0;
  if (!NumberValue(obj, &val))
    return std::nullopt;

  // RGB channels (lanes 0, 1, 2): Int scales by 1/255, Float stays in [0.0, 1.0]
  if (model == ColorModel::kRgbaColor && lane < 3 && obj->GetType() == kIntType)
    return static_cast<float>(val / 255.0);

  return static_cast<float>(val);
}

auto FoldColor(const ColorModel model, Object* c0, Object* c1, Object* c2, Object* alpha) -> Object* {
  const auto v0 = NormalizeColorChannel(model, 0, c0);
  const auto v1 = NormalizeColorChannel(model, 1, c1);
  const auto v2 = NormalizeColorChannel(model, 2, c2);
  const auto va = NormalizeColorChannel(model, 3, alpha);
  if (!v0 || !v1 || !v2 || !va)
    return nullptr;

  return MakeColorObject(model, ColorStorage{*v0, *v1, *v2, *va});
}

auto FoldCastColor(const ColorModel target, Object* src) -> Object* {
  if (!src)
    return nullptr;

#define DEFINE_CASE(Name)                                                                   \
  if (src->GetType() == k##Name##Type) {                                                    \
    const auto& storage = static_cast<Name*>(src)->GetStorage();                            \
    return MakeColorObject(target, FoldColorConvert(storage, ColorModel::k##Name, target)); \
  }
  FOR_EACH_COLOR_MODEL(DEFINE_CASE)
#undef DEFINE_CASE

  return nullptr;
}

auto Builder::Fold(expr::Expr* expr) -> Object* {
  if (!expr)
    return nullptr;

  if (const auto literal = expr->AsLiteral())
    return literal->GetValue();

  if (const auto nc = expr->AsNewColor())
    return FoldColor(nc->GetModel(), Fold(nc->GetC0()), Fold(nc->GetC1()), Fold(nc->GetC2()), Fold(nc->GetAlpha()));

  if (const auto cc = expr->AsCastColor())
    return FoldCastColor(cc->GetTarget(), Fold(cc->GetValue()));

  if (const auto load = expr->AsLoadLocal()) {
    const auto it = constants_.find(load->GetLocal());
    if (it != constants_.end())
      return it->second;
    if (bound_.contains(load->GetLocal()))
      return nullptr;
    return resolver_ ? resolver_(load->GetLocal()) : nullptr;
  }

  if (const auto prop = expr->AsGetProperty()) {
    Object* obj = Fold(prop->GetInstance());
    if (!obj)
      return nullptr;
    const auto& name = prop->GetProperty()->GetPropertyName()->GetValue();

    if (const auto* v2 = dynamic_cast<Vec2*>(obj)) {
      if (name == "x")
        return Float::New(v2->GetValue().x);
      if (name == "y")
        return Float::New(v2->GetValue().y);
    } else if (const auto* v3 = dynamic_cast<Vec3*>(obj)) {
      if (name == "x")
        return Float::New(v3->GetValue().x);
      if (name == "y")
        return Float::New(v3->GetValue().y);
      if (name == "z")
        return Float::New(v3->GetValue().z);
    } else if (const auto* r = dynamic_cast<Rect*>(obj)) {
      if (name == "x")
        return Float::New(r->GetValue().x);
      if (name == "y")
        return Float::New(r->GetValue().y);
      if (name == "w" || name == "width")
        return Float::New(r->GetValue().width);
      if (name == "h" || name == "height")
        return Float::New(r->GetValue().height);
    }

#define DEFINE_COLOR_PROP(Name)                                                                                      \
  if (const auto* c = dynamic_cast<Name*>(obj)) {                                                                    \
    const auto& s = c->GetStorage();                                                                                 \
    if (name == "a" || name == "alpha")                                                                              \
      return Float::New(s.a);                                                                                        \
    if constexpr (ColorModel::k##Name == ColorModel::kRgbaColor || ColorModel::k##Name == ColorModel::kSrgbaColor) { \
      if (name == "r" || name == "red")                                                                              \
        return Float::New(s.c0);                                                                                     \
      if (name == "g" || name == "green")                                                                            \
        return Float::New(s.c1);                                                                                     \
      if (name == "b" || name == "blue")                                                                             \
        return Float::New(s.c2);                                                                                     \
    } else if constexpr (ColorModel::k##Name == ColorModel::kHslaColor) {                                            \
      if (name == "h" || name == "hue")                                                                              \
        return Float::New(s.c0);                                                                                     \
      if (name == "s" || name == "saturation")                                                                       \
        return Float::New(s.c1);                                                                                     \
      if (name == "l" || name == "lightness")                                                                        \
        return Float::New(s.c2);                                                                                     \
    } else if constexpr (ColorModel::k##Name == ColorModel::kHsbaColor) {                                            \
      if (name == "h" || name == "hue")                                                                              \
        return Float::New(s.c0);                                                                                     \
      if (name == "s" || name == "saturation")                                                                       \
        return Float::New(s.c1);                                                                                     \
      if (name == "v" || name == "value" || name == "b" || name == "brightness")                                     \
        return Float::New(s.c2);                                                                                     \
    }                                                                                                                \
    return nullptr;                                                                                                  \
  }
    FOR_EACH_COLOR_MODEL(DEFINE_COLOR_PROP)
#undef DEFINE_COLOR_PROP

    // Multi-channel swizzles (.xy, .rgb, .bgra, .wh, etc.)
    const auto extract_lanes = [&](float out[4], CompileType** out_ty) -> bool {
      if (const auto* v2 = dynamic_cast<Vec2*>(obj)) {
        out[0] = v2->GetValue().x;
        out[1] = v2->GetValue().y;
        *out_ty = CompileType::Vec2();
        return true;
      }
      if (const auto* v3 = dynamic_cast<Vec3*>(obj)) {
        out[0] = v3->GetValue().x;
        out[1] = v3->GetValue().y;
        out[2] = v3->GetValue().z;
        *out_ty = CompileType::Vec3();
        return true;
      }
      if (const auto* r = dynamic_cast<Rect*>(obj)) {
        out[0] = r->GetValue().x;
        out[1] = r->GetValue().y;
        out[2] = r->GetValue().width;
        out[3] = r->GetValue().height;
        *out_ty = CompileType::Rect();
        return true;
      }
#define DEFINE_COLOR_LANES(Name)                  \
  if (const auto* c = dynamic_cast<Name*>(obj)) { \
    const auto& s = c->GetStorage();              \
    out[0] = s.c0;                                \
    out[1] = s.c1;                                \
    out[2] = s.c2;                                \
    out[3] = s.a;                                 \
    *out_ty = CompileType::Name();                \
    return true;                                  \
  }
      FOR_EACH_COLOR_MODEL(DEFINE_COLOR_LANES)
#undef DEFINE_COLOR_LANES
      return false;
    };

    float lanes[4] = {0, 0, 0, 0};
    CompileType* obj_ty = nullptr;
    if (extract_lanes(lanes, &obj_ty)) {
      if (const auto swizzle = ParseSwizzle(obj_ty, name)) {
        const auto& idx = swizzle->indices;
        if (idx.size() == 2)
          return Vec2::New(lanes[idx[0]], lanes[idx[1]]);
        if (idx.size() == 3)
          return Vec3::New(lanes[idx[0]], lanes[idx[1]], lanes[idx[2]]);
        if (idx.size() == 4) {
          if (swizzle->is_color)
            return MakeColorObject(*GetColorModel(*obj_ty),
                                   {lanes[idx[0]], lanes[idx[1]], lanes[idx[2]], lanes[idx[3]]});
          return Rect::New(lanes[idx[0]], lanes[idx[1]], lanes[idx[2]], lanes[idx[3]]);
        }
      }
    }
  }

  if (const auto unary = expr->AsUnary())
    return FoldUnary(unary->GetOp(), Fold(unary->GetValue()));

  if (const auto binary = expr->AsBinary())
    return FoldBinary(binary->GetOp(), Fold(binary->GetLeft()), Fold(binary->GetRight()));

  if (const auto mc = expr->AsMathCall()) {
    Object* a = mc->GetNumberOfArgs() > 0 ? Fold(mc->GetArgAt(0)) : nullptr;
    Object* b = mc->GetNumberOfArgs() > 1 ? Fold(mc->GetArgAt(1)) : nullptr;
    Object* c = mc->GetNumberOfArgs() > 2 ? Fold(mc->GetArgAt(2)) : nullptr;
    return FoldMathCall(mc->GetOp(), a, b, c);
  }

  return nullptr;
}

auto Builder::Build(expr::Expr* expr) -> Node* {
  const auto root = tree_->GetRoot();
  BuildInto(root, expr, {});
  return root;
}

void Builder::BuildInto(Node* parent, expr::Expr* expr, const TagList& tags) {
  if (!expr)
    return;

  if (const auto node = expr->AsNode())
    return BuildElement(parent, node, tags);

  // a sequence renders each of its expressions, in order
  if (const auto seq = expr->AsSeq()) {
    for (const auto child : seq->GetChildren())
      BuildInto(parent, child, tags);
    return;
  }

  if (const auto store = expr->AsStoreLocal())
    return BuildBind(parent, store);

  // a call to a helper that returns a node is that node: it is built here, in its caller's tree
  if (const auto call = expr->AsCall()) {
    if (const auto helper = FindHelper(call))
      return InlineHelper(parent, helper, call, tags);
  }

  // `None` is no node
  if (const auto literal = expr->AsLiteral(); literal && literal->GetValue()->GetType() == kNoneType)
    return;

  const auto hole = tree_->New(Kind::kDynamic);
  hole->SetExpr(expr);
  hole->SetTags(tags);
  parent->Append(hole);
}

void Builder::BuildElement(Node* parent, expr::NodeExpr* expr, const TagList& inherited) {
  // The View is the root of the tree and is already there: a `#view` written in the view says how it looks, so
  // it is built into the root rather than under it. There is only one, and only at the top.
  const auto root = tree_->GetRoot();
  const auto is_view = expr->GetKind() == expr::NodeExpr::Kind::kView;
  const auto only_binds = std::all_of(root->GetChildren().begin(), root->GetChildren().end(), [](const Node* child) {
    return child->IsBind();
  });
  if (is_view && (parent != root || !root->IsView() || !only_binds || !root->GetProperties().empty())) {
    Error(root->IsView() ? "#view is the root of the view: it has to be the outermost element, and there is only one"
                         : "#view can only be the root of a view");
    return;
  }

  const auto node = is_view ? root : tree_->New(ToDomKind(expr->GetKind()));
  if (!is_view)
    parent->Append(node);

  TagList tags = inherited;
  CollectMetaTags(expr->GetDirectives(), &tags);
  node->SetTags(tags);

  // an id names one element of the tree
  if (expr->HasId()) {
    if (const auto other = tree_->FindById(expr->GetId()); other != nullptr && other != node)
      Error("#" + std::string(node->GetTag()) + ": the id '" + expr->GetId() + "' is already used by a #" +
            std::string(other->GetTag()));
    else
      node->SetId(expr->GetId());
  }

  BuildProperties(node, expr);
  ApplyInheritedRules(node, expr);

  if (!CanHaveChildren(node->GetKind())) {
    if (!expr->GetChildren().empty())
      Error("#" + std::string(node->GetTag()) + " can't have children");
    return;
  }
  for (const auto child : expr->GetChildren())
    BuildInto(node, child, tags);
}

void Builder::BuildProperties(Node* node, expr::NodeExpr* expr) {
  const auto where = "#" + std::string(node->GetTag());
  for (const auto store : expr->GetProperties()) {
    const auto& name = store->GetProperty()->GetPropertyName()->GetValue();
    const auto def = FindProperty(node->GetKind(), name);
    if (!def) {
      ReportUnknownProperty(node, name);
      continue;
    }

    if (node->FindProperty(def)) {
      Error(where + ": '" + name + "' is set more than once");
      continue;
    }

    if (!store->GetValue()) {
      Error(where + " '" + name + "': missing value");
      continue;
    }

    PropertyValue value{};
    value.def = def;
    if (store->IsFromSet() && !Fold(store->GetValue())) {
      Error(where + " '" + name + "': the value of a `set` rule has to be a compile-time constant, found " +
            std::string{store->GetValue()->GetName()});
      continue;
    }
    if (const auto folded = Fold(store->GetValue())) {
      if (const auto problem = CheckConstant(*def, folded)) {
        Error(where + " '" + name + "': " + *problem);
        continue;
      }
      value.constant = folded;
    } else {
      value.expr = store->GetValue();
    }
    node->SetProperty(value);
  }
}

// The rules of the calls this element is being built under are defaults for it, as the rules written around it are:
// a property it was given, by itself or by a rule where it is written, is not touched, and of the rules left the
// call nearest to it wins, and in a call the latest rule.
void Builder::ApplyInheritedRules(Node* node, expr::NodeExpr* expr) {
  if (rule_frames_.empty())
    return;

  const auto where = "#" + std::string(node->GetTag());
  for (auto frame = rule_frames_.rbegin(); frame != rule_frames_.rend(); ++frame) {
    for (auto entry = (*frame)->rbegin(); entry != (*frame)->rend(); ++entry) {
      if (entry->kind != expr->GetKind())
        continue;

      const auto def = FindProperty(node->GetKind(), entry->property);
      if (!def) {
        ReportUnknownProperty(node, entry->property);
        continue;
      }
      if (node->FindProperty(def))
        continue;

      const auto folded = Fold(entry->value);
      if (!folded) {
        Error(where + " '" + entry->property + "': the value of a `set` rule has to be a compile-time constant, found " +
              std::string{entry->value->GetName()});
        continue;
      }
      if (const auto problem = CheckConstant(*def, folded)) {
        Error(where + " '" + entry->property + "': " + *problem);
        continue;
      }
      PropertyValue value{};
      value.def = def;
      value.constant = folded;
      node->SetProperty(value);
    }
  }
}

void Builder::BuildBind(Node* parent, expr::StoreLocalExpr* expr) {
  const auto bind = tree_->New(Kind::kBind);
  bind->SetExpr(expr);
  parent->Append(bind);

  // an annotation can change what the value is, so it isn't followed
  const auto constant = expr->GetAnnotation() ? nullptr : Fold(expr->GetValue());
  bound_.insert(expr->GetLocal());
  if (constant)
    constants_[expr->GetLocal()] = constant;
  else
    constants_.erase(expr->GetLocal());  // a helper's parameter holds a different value at each call
}

auto Builder::FindHelper(expr::CallExpr* call) -> Function* {
  if (!functions_)
    return nullptr;

  const auto target = call->GetTarget() ? call->GetTarget()->AsLoadLocal() : nullptr;
  if (!target)
    return nullptr;

  const auto function = functions_(target->GetLocal());
  if (!function || !function->HasBody() || function->IsExtern() || function->IsCExport())
    return nullptr;

  // the entry points are called by the host, never by a view
  const auto name = std::string(function->GetName()->GetData());
  if (name == "view" || name == "init" || name == "update")
    return nullptr;
  return ReturnsNode(function) ? function : nullptr;
}

// A helper is built where it is called: its parameters are bound to the arguments, in the order they are
// given, and then its body is built as if it were written at the call. Every call builds its own copy, so
// what one call is given can't leak into the next. A `#fragment` the helper returns groups its children
// without reserving space, so they lay out as the caller's own.
void Builder::InlineHelper(Node* parent, Function* helper, expr::CallExpr* call, const TagList& tags) {
  const auto name = std::string(helper->GetName()->GetData());
  if (std::find(inlining_.begin(), inlining_.end(), helper) != inlining_.end()) {
    Error("'" + name + "' calls itself: recursion isn't allowed");
    return;
  }

  const auto& params = helper->GetParameters();
  if (params.size() != call->GetNumberOfArgs()) {
    Error("'" + name + "' takes " + std::to_string(params.size()) + " argument(s), but was given " +
          std::to_string(call->GetNumberOfArgs()));
    return;
  }

  inlining_.push_back(helper);
  rule_frames_.push_back(&call->GetInheritedRules());
  for (size_t i = 0; i < params.size(); i++)
    BuildBind(parent, new expr::StoreLocalExpr(params[i].local, call->GetArgAt(i)));
  BuildInto(parent, helper->GetBody(), tags);
  rule_frames_.pop_back();
  inlining_.pop_back();
}

void Builder::ReportUnknownProperty(Node* node, const std::string& name) {
  const auto where = "#" + std::string(node->GetTag());
  const auto elsewhere = std::any_of(AllProperties().begin(), AllProperties().end(), [&](const PropertyDef& def) {
    return def.name == name;
  });
  if (elsewhere) {
    Error(where + ": '" + name + "' doesn't apply to " + where);
    return;
  }

  auto message = where + ": unknown property '" + name + "'";
  if (const auto suggestion = SuggestProperty(node->GetKind(), name))
    message += " (did you mean '" + std::string(suggestion->name) + "'?)";
  Error(message);
}
}  // namespace kura::dom
