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

// The layout properties whose value is only known at run time are patched into the layout's table every
// frame. The runtime only lays out again if one of them actually changed.
void DomLowering::EmitLayoutProperties(dom::Node* node, const Resolved& resolved) {
  const auto id = plan_->FindId(node);
  if (!id)
    return;
  for (const auto& [def, value] : resolved) {
    if (const auto property = LayoutPlan::PropertyOf(def->name))
      host_->Emit(LayoutSetPropertyInstr::New(plan_, *id, *property, value));
  }
}

auto DomLowering::Lower(dom::Node* root) -> bool {
  std::string error{};
  plan_ = LayoutPlan::Build(root, &error);
  if (!plan_)
    return Fail(error);

  if (!root->Accept(this))
    return false;

  host_->Emit(LayoutRunInstr::New(plan_));
  for (const auto& op : ops_) {
    if (op.node->IsButton()) {
      if (!DrawButton(op.node))
        return false;
    } else if (op.node->IsText()) {
      if (!DrawText(op))
        return false;
    } else if (op.node->IsBlock()) {
      if (!DrawBlock(op.node))
        return false;
    } else if (op.node->IsDynamic()) {
      if (!LowerDynamic(op.node))
        return false;
    }
  }
  return true;
}

auto DomLowering::VisitNode(dom::Node* node) -> bool {
  if (!ResolveProperties(node, &resolved_[node]))
    return false;

  EmitLayoutProperties(node, resolved_[node]);
  return VisitChildren(node);
}

auto DomLowering::VisitBlock(dom::Node* node) -> bool {
  if (!ResolveProperties(node, &resolved_[node]))
    return false;

  EmitLayoutProperties(node, resolved_[node]);
  ops_.push_back(Op{node, nullptr});
  return VisitChildren(node);
}

auto DomLowering::VisitButton(dom::Node* node) -> bool {
  if (!ResolveProperties(node, &resolved_[node]))
    return false;

  EmitLayoutProperties(node, resolved_[node]);
  ops_.push_back(Op{node, nullptr});
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
  auto& resolved = resolved_[node];
  if (!ResolveProperties(node, &resolved))
    return false;

  EmitLayoutProperties(node, resolved);
  auto value = ResolvePropertyByName(node, "value", resolved);
  if (!value)
    value = host_->Bind(ConstantInstr::New(String::New("")));

  // Layout needs to know how big the text is going to be, so it has to be told before it runs.
  const auto id = plan_->FindId(node);
  if (id)
    host_->Emit(LayoutSetTextInstr::New(plan_, *id, value, plan_->GetFontSize(*id), plan_->GetFontFamily(*id)));

  ops_.push_back(Op{node, value});
  return true;
}

auto DomLowering::VisitDynamic(dom::Node* node) -> bool {
  ops_.push_back(Op{node, nullptr});
  return true;
}

auto DomLowering::VisitBind(dom::Node* node) -> bool {
  return host_->EvalForEffect(node->GetExpr());
}

auto DomLowering::Box(dom::Node* node, const int component) -> Value* {
  const auto id = plan_->FindId(node);
  if (!id) {
    Fail("internal error: #" + std::string(node->GetTag()) + " has no place in the layout");
    return nullptr;
  }

  return host_->Bind(LayoutBoxInstr::New(plan_, *id, static_cast<LayoutBoxInstr::Component>(component)));
}

auto DomLowering::DrawBlock(dom::Node* node) -> bool {
  Value* x = Box(node, LayoutBoxInstr::kX);
  Value* y = Box(node, LayoutBoxInstr::kY);
  Value* w = Box(node, LayoutBoxInstr::kWidth);
  Value* h = Box(node, LayoutBoxInstr::kHeight);
  if (!x || !y || !w || !h)
    return false;

  Value* color = ResolvePropertyByName(node, "fill", resolved_[node]);
  if (!color)
    color = host_->Bind(ConstantInstr::New(Int::New(0)));

  EmitRenderCall(FillRectInstr::New(x, y, w, h, color));
  return true;
}

auto DomLowering::DrawButton(dom::Node* node) -> bool {
  Value* x = Box(node, LayoutBoxInstr::kX);
  Value* y = Box(node, LayoutBoxInstr::kY);
  Value* w = Box(node, LayoutBoxInstr::kWidth);
  Value* h = Box(node, LayoutBoxInstr::kHeight);
  if (!x || !y || !w || !h)
    return false;

  Value* color = ResolvePropertyByName(node, "fill", resolved_[node]);
  if (!color)
    color = host_->Bind(ConstantInstr::New(Int::New(0)));

  EmitRenderCall(FillRectInstr::New(x, y, w, h, color));
  return true;
}

auto DomLowering::DrawText(const Op& op) -> bool {
  const auto id = plan_->FindId(op.node);
  if (!id)
    return Fail("internal error: #" + std::string(op.node->GetTag()) + " has no place in the layout");

  // Text is opaque black unless it says otherwise (0xRRGGBBAA).
  Value* color = ResolvePropertyByName(op.node, "color", resolved_[op.node]);
  if (!color)
    color = host_->Bind(ConstantInstr::New(Int::New(0x000000FF)));

  EmitRenderCall(DrawTextInstr::New(plan_, *id, op.text, color));
  return true;
}

auto DomLowering::LowerDynamic(dom::Node* node) -> bool {
  NodeVisitor for_expr(host_->GetOwner());
  return node->GetExpr()->Accept(&for_expr);
}
}  // namespace kura
