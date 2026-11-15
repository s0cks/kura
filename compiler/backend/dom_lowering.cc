#include "backend/dom_lowering.h"

#include "backend/gir.h"
#include "backend/node_visitor.h"
#include "backend/runtime_abi.h"
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
    // An event's value is a Message, evaluated like any other property's (`on-click: Increment` builds
    // `Increment`), and handed to the runtime where the node is drawn: see EmitEvents.
    if (property.IsConstant())
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
  if (!id || drawing_hole_)
    return;
  // A scroll container is a viewport and the content in it (the next node): what makes it a container is the
  // content's, and what the viewport has is what makes it a box. The direction is both's.
  const auto scrolls = node->IsScroll() || node->IsList();
  for (const auto& [def, value] : resolved) {
    const auto property = LayoutPlan::PropertyOf(def->name);
    if (!property)
      continue;

    const std::string_view name = def->name;
    const auto container = name == "padding" || name == "gap" || name == "align" || name == "justify" || name == "wrap";
    if (scrolls && (container || name == "direction"))
      host_->Emit(LayoutSetPropertyInstr::New(plan_, *id + 1, *property, value));
    if (!scrolls || !container)
      host_->Emit(LayoutSetPropertyInstr::New(plan_, *id, *property, value));
  }
}

auto DomLowering::Lower(dom::Node* root) -> bool {
  const auto owner = host_->GetOwner();
  // when this tree is what a hole makes, where: the hole's own scope must not reach what is lowered in here
  const auto hole = owner->hole_;
  struct Restore {
    FlowGraphBuilder* owner;
    const FlowGraphBuilder::HoleScope* hole;
    ~Restore() {
      owner->hole_ = hole;
    }
  } restore{owner, hole};
  owner->hole_ = nullptr;

  std::string error{};
  plan_ = hole ? LayoutPlan::BuildTemplate(root, &error) : LayoutPlan::Build(root, &error);
  if (!plan_)
    return Fail(error);

  if (hole) {
    drawing_hole_ = hole->draw;
    host_->Emit(LayoutSlotInstanceInstr::New(hole->plan, hole->slot, plan_));
  }

  if (!root->Accept(this))
    return false;

  // what a hole makes is drawn when the hole is drawn, after the layout of the tree it is in
  if (hole && !hole->draw)
    return true;

  // the tree a hole is in lays its instances out, with itself
  if (!hole)
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
    } else if (op.node->IsView()) {
      if (!DrawView(op.node))
        return false;
    } else if (op.node->IsScroll() || op.node->IsList()) {
      if (!DrawScroll(op))
        return false;
    } else if (op.node->IsLine()) {
      if (!DrawLine(*op.node))
        return false;
    } else if (op.node->IsDynamic()) {
      if (!RunHole(op.node, true))
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

auto DomLowering::VisitLine(dom::Node* node) -> bool {
  auto& resolved = resolved_[node];
  if (!ResolveProperties(node, &resolved))
    return false;

  EmitLayoutProperties(node, resolved);
  ops_.push_back(Op{node, nullptr, true});
  return true;
}

auto DomLowering::VisitBlock(dom::Node* node) -> bool {
  if (!ResolveProperties(node, &resolved_[node]))
    return false;

  EmitLayoutProperties(node, resolved_[node]);
  ops_.push_back(Op{node, nullptr});
  return VisitChildren(node);
}

auto DomLowering::VisitView(dom::Node* node) -> bool {
  if (!ResolveProperties(node, &resolved_[node]))
    return false;

  EmitLayoutProperties(node, resolved_[node]);
  ops_.push_back(Op{node, nullptr});
  return VisitChildren(node);
}

auto DomLowering::VisitScrollable(dom::Node* node) -> bool {
  if (!ResolveProperties(node, &resolved_[node]))
    return false;

  EmitLayoutProperties(node, resolved_[node]);
  ops_.push_back(Op{node, nullptr, false});
  if (!VisitChildren(node))
    return false;
  ops_.push_back(Op{node, nullptr, true});
  return true;
}

auto DomLowering::VisitScroll(dom::Node* node) -> bool {
  return VisitScrollable(node);
}

auto DomLowering::VisitList(dom::Node* node) -> bool {
  return VisitScrollable(node);
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
  if (id && !drawing_hole_)
    host_->Emit(LayoutSetTextInstr::New(plan_, *id, value, plan_->GetFontSize(*id), plan_->GetFontFamily(*id)));

  ops_.push_back(Op{node, value});
  return true;
}

auto DomLowering::VisitDynamic(dom::Node* node) -> bool {
  ops_.push_back(Op{node, nullptr});
  // its instances are made now, before the layout; when they are drawn, they are there already
  if (drawing_hole_)
    return true;
  return RunHole(node, false);
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

// What a click on the node means. The runtime keeps the node's box for the frame, with the Message, and the host
// looks up where a click landed. Nodes are drawn parents first, so the last box to hold a point is the topmost.
void DomLowering::EmitEvents(dom::Node* node) {
  const auto id = plan_->FindId(node);
  if (!id)
    return;
  const auto& resolved = resolved_[node];
  auto scroll_message = false;
  for (const auto& property : node->GetProperties()) {
    if (!property.def->event)
      continue;

    const std::string_view name = property.def->name;
    const auto kind = name == "on-click" ? rt::kEventClick : name == "on-scroll" ? rt::kEventScroll : 0;
    if (kind == 0)
      continue;
    if (const auto it = resolved.find(property.def); it != resolved.end()) {
      host_->Emit(RegisterEventInstr::New(plan_, *id, kind, it->second));
      scroll_message |= kind == rt::kEventScroll;
    }
  }

  // a container scrolls with or without a Message for it
  if ((node->IsScroll() || node->IsList()) && !scroll_message)
    host_->Emit(RegisterEventInstr::New(plan_, *id, rt::kEventScroll, host_->Bind(ConstantInstr::New(None::Get()))));
}

// Paints the box of a node: its `fill`, and what makes it more than a rectangle, which is `border-width`,
// `border-color`, `radius` and `opacity`. A box with none of those is the plain rectangle it always was. A box that
// isn't `always` painted, because it has no color of its own, paints nothing without them.
//
// A border takes its width and color from what the node says, and where only one is said: a color alone is a
// 1px border, and a width alone is black.
void DomLowering::EmitBox(dom::Node* node, Value* x, Value* y, Value* w, Value* h, const bool always) {
  auto& resolved = resolved_[node];
  Value* fill = ResolvePropertyByName(node, "fill", resolved);
  Value* border_color = ResolvePropertyByName(node, "border-color", resolved);
  Value* border_width = ResolvePropertyByName(node, "border-width", resolved);
  Value* radius = ResolvePropertyByName(node, "radius", resolved);
  Value* opacity = ResolvePropertyByName(node, "opacity", resolved);
  const auto constant = [this](Object* value) { return host_->Bind(ConstantInstr::New(value)); };

  if (!border_color && !border_width && !radius && !opacity) {
    if (!fill && !always)
      return;
    EmitRenderCall(FillRectInstr::New(x, y, w, h, fill ? fill : constant(Int::New(0))));
    return;
  }

  if (border_width && !border_color)
    border_color = constant(Int::New(0x000000FF));
  if (border_color && !border_width)
    border_width = constant(Float::New(1.0));
  EmitRenderCall(PaintBoxInstr::New(x, y, w, h, fill ? fill : constant(Int::New(0)),
                                    border_color ? border_color : constant(Int::New(0)),
                                    border_width ? border_width : constant(Float::New(0.0)),
                                    radius ? radius : constant(Float::New(0.0)),
                                    opacity ? opacity : constant(Float::New(1.0))));
}

auto DomLowering::DrawBlock(dom::Node* node) -> bool {
  Value* x = Box(node, LayoutBoxInstr::kX);
  Value* y = Box(node, LayoutBoxInstr::kY);
  Value* w = Box(node, LayoutBoxInstr::kWidth);
  Value* h = Box(node, LayoutBoxInstr::kHeight);
  if (!x || !y || !w || !h)
    return false;

  EmitBox(node, x, y, w, h, true);
  EmitEvents(node);
  return true;
}

// The View is the window: it paints its `fill` over all of it, if it has one, and takes the clicks nothing in it does.
auto DomLowering::DrawView(dom::Node* node) -> bool {
  Value* x = Box(node, LayoutBoxInstr::kX);
  Value* y = Box(node, LayoutBoxInstr::kY);
  Value* w = Box(node, LayoutBoxInstr::kWidth);
  Value* h = Box(node, LayoutBoxInstr::kHeight);
  if (!x || !y || !w || !h)
    return false;

  EmitBox(node, x, y, w, h, false);
  EmitEvents(node);
  return true;
}

auto DomLowering::DrawLine(dom::Node& node) -> bool {
  Value* x = Box(&node, LayoutBoxInstr::kX);
  Value* y = Box(&node, LayoutBoxInstr::kY);
  Value* w = Box(&node, LayoutBoxInstr::kWidth);
  Value* h = Box(&node, LayoutBoxInstr::kHeight);
  if (!x || !y || !w || !h)
    return false;

  Value* half = host_->Bind(ConstantInstr::New(Float::New(0.5f)));

  Value* x1 = x;
  Value* y1 = host_->Bind(BinaryOpInstr::New(BinaryOp::kMultiply, h, half));
  y1 = host_->Bind(BinaryOpInstr::New(BinaryOp::kAdd, y, y1));

  Value* x2 = host_->Bind(BinaryOpInstr::New(BinaryOp::kAdd, x, w));
  Value* y2 = y1;

  Value* color = ResolvePropertyByName(&node, "fill", resolved_[&node]);
  if (!color)
    color = host_->Bind(ConstantInstr::New(Int::New(0)));

  EmitRenderCall(DrawLineInstr::New(x1, y1, x2, y2, color));
  return true;
}

// A scroll container paints its `fill`, says where the wheel and clicks mean something, and then clips what is in it
// to its box until the mark that ends it.
auto DomLowering::DrawScroll(const Op& op) -> bool {
  Value* x = Box(op.node, LayoutBoxInstr::kX);
  Value* y = Box(op.node, LayoutBoxInstr::kY);
  Value* w = Box(op.node, LayoutBoxInstr::kWidth);
  Value* h = Box(op.node, LayoutBoxInstr::kHeight);
  if (!x || !y || !w || !h)
    return false;

  if (op.end) {
    EmitRenderCall(PopClipInstr::New());
    return true;
  }

  EmitBox(op.node, x, y, w, h, false);
  EmitEvents(op.node);
  EmitRenderCall(PushClipInstr::New(x, y, w, h));
  return true;
}

auto DomLowering::DrawButton(dom::Node* node) -> bool {
  Value* x = Box(node, LayoutBoxInstr::kX);
  Value* y = Box(node, LayoutBoxInstr::kY);
  Value* w = Box(node, LayoutBoxInstr::kWidth);
  Value* h = Box(node, LayoutBoxInstr::kHeight);
  if (!x || !y || !w || !h)
    return false;

  EmitBox(node, x, y, w, h, true);
  EmitEvents(node);
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

  Value* opacity = ResolvePropertyByName(op.node, "opacity", resolved_[op.node]);
  if (!opacity)
    opacity = host_->Bind(ConstantInstr::New(Float::New(1.0)));

  // Where each line of the text is in its room is worked out when it is drawn, from a word that is known now.
  auto align = DrawTextInstr::kAlignStart;
  if (const auto property = op.node->FindProperty("text-align")) {
    if (!property->IsConstant())
      return Fail("#text 'text-align' has to be a constant for now: it is a word like \"center\", not a value that changes");
    const auto word = static_cast<String*>(property->constant)->GetValue();
    align = word == "center" ? DrawTextInstr::kAlignCenter : word == "right" ? DrawTextInstr::kAlignEnd
                                                                              : DrawTextInstr::kAlignStart;
  }

  EmitRenderCall(DrawTextInstr::New(plan_, *id, op.text, color, opacity, align));
  return true;
}

// Runs a dynamic hole: the first time, to make its instances under its slot, and the second, to draw them.
auto DomLowering::RunHole(dom::Node* node, const bool draw) -> bool {
  const auto id = plan_->FindId(node);
  if (!id)
    return Fail("internal error: a dynamic node has no place in the layout");

  const auto owner = host_->GetOwner();
  FlowGraphBuilder::HoleScope scope{plan_, *id, draw};
  const auto outer = owner->hole_;
  owner->hole_ = &scope;

  host_->Emit(LayoutSlotBeginInstr::New(plan_, *id));
  NodeVisitor for_expr(owner);
  const auto ok = node->GetExpr()->Accept(&for_expr);
  owner->hole_ = outer;
  if (!ok)
    return false;

  if (!draw)
    host_->Emit(LayoutSlotEndInstr::New(plan_, *id));
  return true;
}
}  // namespace kura
