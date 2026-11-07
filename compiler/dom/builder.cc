#include "dom/builder.h"

#include <algorithm>

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

auto FoldBinary(const BinaryOp op, Object* lhs, Object* rhs) -> Object* {
  if (!lhs || !rhs || lhs->GetType() != rhs->GetType())
    return nullptr;

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
}  // namespace

void Builder::Error(const std::string& message) {
  errors_.push_back(message);
}

auto Builder::Fold(expr::Expr* expr) -> Object* {
  if (!expr)
    return nullptr;

  if (const auto literal = expr->AsLiteral())
    return literal->GetValue();

  if (const auto load = expr->AsLoadLocal()) {
    const auto it = constants_.find(load->GetLocal());
    if (it != constants_.end())
      return it->second;
    return resolver_ ? resolver_(load->GetLocal()) : nullptr;
  }

  if (const auto unary = expr->AsUnary())
    return FoldUnary(unary->GetOp(), Fold(unary->GetValue()));

  if (const auto binary = expr->AsBinary())
    return FoldBinary(binary->GetOp(), Fold(binary->GetLeft()), Fold(binary->GetRight()));

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

  // `None` is no node
  if (const auto literal = expr->AsLiteral(); literal && literal->GetValue()->GetType() == kNoneType)
    return;

  const auto hole = tree_->New(Kind::kDynamic);
  hole->SetExpr(expr);
  hole->SetTags(tags);
  parent->Append(hole);
}

void Builder::BuildElement(Node* parent, expr::NodeExpr* expr, const TagList& inherited) {
  const auto node = tree_->New(ToDomKind(expr->GetKind()));
  parent->Append(node);

  TagList tags = inherited;
  CollectMetaTags(expr->GetDirectives(), &tags);
  node->SetTags(tags);

  BuildProperties(node, expr);

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

void Builder::BuildBind(Node* parent, expr::StoreLocalExpr* expr) {
  const auto bind = tree_->New(Kind::kBind);
  bind->SetExpr(expr);
  parent->Append(bind);

  // an annotation can change what the value is, so it isn't followed
  if (!expr->GetAnnotation()) {
    if (const auto constant = Fold(expr->GetValue()))
      constants_[expr->GetLocal()] = constant;
  }
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
