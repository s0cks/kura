#include "backend/dom_lowering.h"

#include "backend/gir.h"
#include "backend/node_visitor.h"
#include "object.h"

namespace kura {
auto DomLowering::Fail(const std::string& message) -> bool {
  host_->Fail(message);
  return false;
}

void DomLowering::EmitRenderCall(Definition* defn) {
#ifdef KURA_DEBUG
  host_->Emit(CheckRendererStatusInstr::New(host_->Bind(defn)));
#else
  host_->Emit(defn);
#endif  // KURA_DEBUG
}

auto DomLowering::ResolveProperties(dom::Node* node, Resolved* resolved) -> bool {
  for (const auto& property : node->GetProperties()) {
    if (property.IsConstant() || property.def->event)
      continue;

    const auto value = host_->Eval(property.expr);
    if (!value)
      return false;

    host_->GetOwner()->AddPropertyConstraint(
        value, property.def, "#" + std::string(node->GetTag()) + " '" + std::string(property.def->name) + "'");
    (*resolved)[property.def] = value;
  }
  return true;
}

auto DomLowering::VisitNode(dom::Node* node) -> bool {
  Resolved resolved{};
  if (!ResolveProperties(node, &resolved))
    return false;

  return VisitChildren(node);
}

auto DomLowering::VisitButton(dom::Node* node) -> bool {
  Resolved resolved{};
  if (!ResolveProperties(node, &resolved))
    return false;

  Value* x = host_->Bind(ConstantInstr::New(Int::New(1)));
  Value* y = host_->Bind(ConstantInstr::New(Int::New(2)));
  Value* w = host_->Bind(ConstantInstr::New(Int::New(3)));
  Value* h = host_->Bind(ConstantInstr::New(Int::New(4)));
  Value* color = ResolvePropertyByName(node, "fill", resolved);
  if (!color)
    color = host_->Bind(ConstantInstr::New(Int::New(0)));

  EmitRenderCall(FillRectInstr::New(x, y, w, h, color));
  return VisitChildren(node);
}

auto DomLowering::ResolvePropertyByName(dom::Node* node, const std::string name, Resolved& resolved) -> Value* {
  Value* value = nullptr;
  if (const auto property = node->FindProperty(name)) {
    if (property->IsConstant()) {
      value = host_->Bind(ConstantInstr::New(property->constant));
    } else {
      value = resolved[property->def];
    }
  }

  return value;
}

auto DomLowering::VisitText(dom::Node* node) -> bool {
  Resolved resolved{};
  if (!ResolveProperties(node, &resolved))
    return false;

  Value* value = nullptr;
  if (const auto property = node->FindProperty("value")) {
    if (property->IsConstant()) {
      value = host_->Bind(ConstantInstr::New(property->constant));
    } else {
      value = resolved[property->def];
    }
  }

  if (!value)
    value = host_->Bind(ConstantInstr::New(String::New("")));

  EmitRenderCall(DrawTextInstr::New(value));
  return true;
}

auto DomLowering::VisitDynamic(dom::Node* node) -> bool {
  NodeVisitor for_expr(host_->GetOwner());
  return node->GetExpr()->Accept(&for_expr);
}

auto DomLowering::VisitBind(dom::Node* node) -> bool {
  return host_->EvalForEffect(node->GetExpr());
}
}  // namespace kura
