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

    // `on-input: Typed` names the variant that the typed text goes in, so it has no payload here: the runtime gives
    // it one every time the text changes.
    Value* value = nullptr;
    const auto variant = property.def->name == "on-input" && property.expr ? property.expr->AsVariant() : nullptr;
    if (variant && !variant->HasPayload())
      value = host_->Bind(NewVariantInstr::NewOpen(variant->GetVariantName()));
    else
      value = host_->Eval(property.expr);
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

auto DomLowering::EmitPassRange(const size_t begin, const size_t end) -> bool {
  if (pass_ == PaintPass::kAll || pass_ == PaintPass::kBoxesOnly) {
    for (size_t i = begin; i < end; i++) {
      const auto& op = ops_[i];
      if (op.node->IsButton()) {
        if (!DrawButton(op.node))
          return false;
      } else if (op.node->IsBlock()) {
        if (!DrawBlock(op.node))
          return false;
      } else if (op.node->IsView()) {
        if (!DrawView(op.node))
          return false;
      } else if (op.node->IsLine()) {
        if (!DrawLine(*op.node))
          return false;
      } else if (op.node->IsInput()) {
        if (!DrawInputBox(op.node))
          return false;
      } else if (op.node->IsImage()) {
        if (!DrawImage(op.node))
          return false;
      } else if (op.node->IsList()) {
        if (!DrawBlock(op.node))
          return false;
      }
    }
  }

  // 2. Text (all labels in this clip section emit together)
  if (pass_ == PaintPass::kAll || pass_ == PaintPass::kTextOnly) {
    for (size_t i = begin; i < end; i++) {
      const auto& op = ops_[i];
      if (op.node->IsText()) {
        if (!DrawText(op))
          return false;
      } else if (op.node->IsInput()) {
        if (!DrawInputText(op))
          return false;
      }
    }
  }

  // 3. Dynamic holes (`for`, `if`, `match`)
  if (pass_ == PaintPass::kAll) {
    for (size_t i = begin; i < end; i++) {
      const auto& op = ops_[i];
      if (op.node->IsDynamic()) {
        if (!RunHole(op.node, true))
          return false;
      }
    }
  }

  return true;
}

auto DomLowering::Lower(dom::Node* root) -> bool {
  const auto owner = host_->GetOwner();
  // when this tree is what a hole makes, where: the hole's own scope must not reach what is lowered in here
  const auto hole = owner->hole_;
  hole_ = hole;
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
    pass_ = hole->pass == 1 ? PaintPass::kBoxesOnly : hole->pass == 2 ? PaintPass::kTextOnly : PaintPass::kAll;
    if (hole->clip)
      clip_stack_.push_back(hole->clip);
    host_->Emit(LayoutSlotInstanceInstr::New(hole->plan, hole->slot, plan_));
  }

  if (!root->Accept(this))
    return false;

  if (hole && !hole->draw)
    return true;

  if (!hole)
    host_->Emit(LayoutRunInstr::New(plan_));

  // Flush each clip section in pass order (Boxes -> Text -> Holes)
  size_t section_start = 0;
  for (size_t i = 0; i < ops_.size(); i++) {
    const auto& op = ops_[i];
    if (op.node->IsScroll()) {
      if (!EmitPassRange(section_start, i))
        return false;
      if (!DrawScroll(op))
        return false;
      section_start = i + 1;
    }
  }

  if (drawing_hole_ && CurrentClip() != nullptr)
    return EmitClippedHoleBody(root, section_start, ops_.size());

  return EmitPassRange(section_start, ops_.size());
}

auto DomLowering::EmitClippedHoleBody(dom::Node* root, const size_t begin, const size_t end) -> bool {
  dom::Node* target_node = (begin < end && ops_[begin].node) ? ops_[begin].node : root;
  Value* item_rect = BoxOf(target_node);
  if (!item_rect)
    return false;

  Value* visible = host_->Bind(RectIntersectsInstr::New(item_rect, CurrentClip()));

  const auto owner = host_->GetOwner();
  auto* then_entry = owner->NewTarget("clip.draw");
  auto* else_entry = owner->NewTarget("clip.skip");
  auto* join_entry = owner->NewJoin("clip.join");

  owner->Terminate(BranchInstr::New(visible, then_entry, else_entry, join_entry));

  // Then branch: item intersects the scroll clip rect -> emit draw calls
  owner->Enter(then_entry);
  if (!EmitPassRange(begin, end))
    return false;
  if (!owner->IsClosed())
    owner->Terminate(GotoInstr::New(join_entry));

  // Else branch: offscreen -> skip draw calls completely
  owner->Enter(else_entry);
  if (hole_ && hole_->monotonic_forward && hole_->exit != nullptr) {
    // axis = 1 for vertical column list (`item.y > clip.y + clip.height`), 0 for horizontal row
    Value* past_end = host_->Bind(RectPastClipInstr::New(item_rect, CurrentClip()));
    auto* break_entry = owner->NewTarget("clip.break");
    auto* cont_entry = owner->NewTarget("clip.continue");

    owner->Terminate(BranchInstr::New(past_end, break_entry, cont_entry, nullptr));

    // Past bottom/right of viewport -> jump directly out of the `for` loop!
    owner->Enter(break_entry);
    owner->Terminate(GotoInstr::New(hole_->exit));

    // Before top/left of viewport -> skip this item and keep scanning
    owner->Enter(cont_entry);
    owner->Terminate(GotoInstr::New(join_entry));
  } else {
    owner->Terminate(GotoInstr::New(join_entry));
  }

  // Continue lowering at the join block (graph_->Finalize() wires predecessor/successor edges)
  owner->Enter(join_entry);
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
  if (id && !drawing_hole_)
    host_->Emit(LayoutSetTextInstr::New(plan_, *id, value, plan_->GetFontSize(*id), plan_->GetFontFamily(*id)));

  ops_.push_back(Op{node, value});
  return true;
}

// An input is a box, painted with the boxes, and its text, painted with the text over every box in the clip. Its
// value is what layout makes room for, as a text's is, and what the view says it is.
auto DomLowering::VisitInput(dom::Node* node) -> bool {
  auto& resolved = resolved_[node];
  if (!ResolveProperties(node, &resolved))
    return false;

  EmitLayoutProperties(node, resolved);
  auto value = ResolvePropertyByName(node, "value", resolved);
  if (!value)
    value = host_->Bind(ConstantInstr::New(String::New("")));

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

auto DomLowering::BoxOf(dom::Node* node) -> Value* {
  const auto id = plan_->FindId(node);
  if (!id) {
    Fail("internal error: #" + std::string(node->GetTag()) + " has no place in the layout");
    return nullptr;
  }

  return host_->Bind(LayoutRectInstr::New(plan_, *id));
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

  EmitTooltip(node);
}

// A node's `tooltip` is a place where the pointer has to be for it to show, the node's box with the text as what
// it means: the runtime keeps the places of a frame, and draws the one the pointer is over when the frame ends.
void DomLowering::EmitTooltip(dom::Node* node) {
  const auto id = plan_->FindId(node);
  if (!id)
    return;
  if (const auto text = ResolvePropertyByName(node, "tooltip", resolved_[node]))
    host_->Emit(RegisterEventInstr::New(plan_, *id, rt::kEventTooltip, text));
}

// Paints the box of a node: its `fill`, and what makes it more than a rectangle, which is `border-width`,
// `border-color`, `radius` and `opacity`. A box with none of those is the plain rectangle it always was. A box that
// isn't `always` painted, because it has no color of its own, paints nothing without them.
//
// A border takes its width and color from what the node says, and where only one is said: a color alone is a
// 1px border, and a width alone is black.
auto DomLowering::EmitBox(dom::Node* node, const bool always, Value* rect) -> bool {
  auto& resolved = resolved_[node];
  Value* fill = ResolvePropertyByName(node, "fill", resolved);
  Value* border_color = ResolvePropertyByName(node, "border-color", resolved);
  Value* border_width = ResolvePropertyByName(node, "border-width", resolved);
  Value* radius = ResolvePropertyByName(node, "radius", resolved);
  Value* opacity = ResolvePropertyByName(node, "opacity", resolved);
  const auto constant = [this](Object* value) {
    return host_->Bind(ConstantInstr::New(value));
  };

  if (!border_color && !border_width && !radius && !opacity) {
    if (!fill && !always)
      return true;
    if (!rect && !(rect = BoxOf(node)))
      return false;
    EmitRenderCall(FillRectInstr::New(rect, fill ? fill : constant(Int::New(0))));
    return true;
  }

  if (border_width && !border_color)
    border_color = constant(Int::New(0x000000FF));
  if (border_color && !border_width)
    border_width = constant(Float::New(1.0));
  if (!rect && !(rect = BoxOf(node)))
    return false;
  EmitRenderCall(
      PaintBoxInstr::New(rect, fill ? fill : constant(Int::New(0)), border_color ? border_color : constant(Int::New(0)),
                         border_width ? border_width : constant(Float::New(0.0)),
                         radius ? radius : constant(Float::New(0.0)), opacity ? opacity : constant(Float::New(1.0))));
  return true;
}

auto DomLowering::DrawBlock(dom::Node* node) -> bool {
  if (!EmitBox(node, false))
    return false;
  EmitEvents(node);
  return true;
}

// The View is the window: it paints its `fill` over all of it, if it has one, and takes the clicks nothing in it does.
auto DomLowering::DrawView(dom::Node* node) -> bool {
  if (!EmitBox(node, false))
    return false;
  EmitEvents(node);
  return true;
}

auto DomLowering::DrawLine(dom::Node& node) -> bool {
  Value* rect = BoxOf(&node);
  if (!rect)
    return false;

  Value* color = ResolvePropertyByName(&node, "fill", resolved_[&node]);
  if (!color)
    color = host_->Bind(ConstantInstr::New(Int::New(0)));

  EmitRenderCall(DrawLineInstr::New(rect, color));
  return true;
}

// A scroll container paints its `fill`, says where the wheel and clicks mean something, and then clips what is in it
// to its box until the mark that ends it.
auto DomLowering::DrawScroll(const Op& op) -> bool {
  if (op.end) {
    EmitRenderCall(PopClipInstr::New());
    if (!clip_stack_.empty())
      clip_stack_.pop_back();
    return true;
  }

  Value* rect = BoxOf(op.node);
  if (!rect)
    return false;

  if (!EmitBox(op.node, false, rect))
    return false;

  EmitEvents(op.node);
  EmitRenderCall(PushClipInstr::New(rect));
  clip_stack_.push_back(rect);
  return true;
}

auto DomLowering::DrawButton(dom::Node* node) -> bool {
  if (!EmitBox(node, true))
    return false;
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
      return Fail(
          "#text 'text-align' has to be a constant for now: it is a word like \"center\", not a value that changes");
    const auto word = static_cast<String*>(property->constant)->GetValue();
    align = word == "center" ? DrawTextInstr::kAlignCenter
          : word == "right"  ? DrawTextInstr::kAlignEnd
                             : DrawTextInstr::kAlignStart;
  }

  EmitRenderCall(DrawTextInstr::New(plan_, *id, op.text, color, opacity, align));
  EmitTooltip(op.node);
  return true;
}

// The box of an input: what it says is its `fill` and border, and a click on it is a click on its box like any
// other's. The text is drawn later, with the others'.
auto DomLowering::DrawInputBox(dom::Node* node) -> bool {
  if (!EmitBox(node, false))
    return false;

  EmitEvents(node);
  return true;
}

auto DomLowering::DrawImage(dom::Node* node) -> bool {
  if (!EmitBox(node, false))
    return false;

  auto& resolved = resolved_[node];

  Value* source = ResolvePropertyByName(node, "source", resolved);
  if (!source)
    return Fail("internal error: #" + std::string(node->GetTag()) + " has source value");

  EmitRenderCall(DrawImageInstr::New(source));
  EmitEvents(node);
  return true;
}

auto DomLowering::DrawInputText(const Op& op) -> bool {
  const auto id = plan_->FindId(op.node);
  if (!id)
    return Fail("internal error: #" + std::string(op.node->GetTag()) + " has no place in the layout");

  auto& resolved = resolved_[op.node];
  const auto constant = [this](Object* value) {
    return host_->Bind(ConstantInstr::New(value));
  };

  Value* color = ResolvePropertyByName(op.node, "color", resolved);
  if (!color)
    color = constant(Int::New(0x000000FF));
  Value* opacity = ResolvePropertyByName(op.node, "opacity", resolved);
  if (!opacity)
    opacity = constant(Float::New(1.0));
  Value* placeholder = ResolvePropertyByName(op.node, "placeholder", resolved);
  if (!placeholder)
    placeholder = constant(String::New(""));
  Value* readonly = ResolvePropertyByName(op.node, "readonly", resolved);
  if (!readonly)
    readonly = constant(Bool::False());

  // what `on-input` is, or None: without it there is nowhere for what is typed to go, and the input only shows
  Value* message = nullptr;
  if (const auto property = op.node->FindProperty("on-input")) {
    if (const auto it = resolved.find(property->def); it != resolved.end())
      message = it->second;
  }
  if (!message)
    message = constant(None::Get());

  auto align = DrawTextInstr::kAlignStart;
  if (const auto property = op.node->FindProperty("text-align")) {
    if (!property->IsConstant())
      return Fail(
          "#input 'text-align' has to be a constant for now: it is a word like \"center\", not a value that changes");
    const auto word = static_cast<String*>(property->constant)->GetValue();
    align = word == "center" ? DrawTextInstr::kAlignCenter
          : word == "right"  ? DrawTextInstr::kAlignEnd
                             : DrawTextInstr::kAlignStart;
  }

  EmitRenderCall(DrawInputInstr::New(plan_, *id, op.text, color, opacity, placeholder, message, readonly, align));
  EmitTooltip(op.node);
  return true;
}

// Runs a dynamic hole: the first time, to make its instances under its slot, and the second, to draw them.
auto DomLowering::RunHole(dom::Node* node, const bool draw) -> bool {
  const auto id = plan_->FindId(node);
  if (!id)
    return Fail("internal error: a dynamic node has no place in the layout");

  const auto owner = host_->GetOwner();
  const auto outer = owner->hole_;

  bool monotonic_forward = false;
  const auto& nodes = plan_->GetNodes();
  if (*id < nodes.size() && nodes[*id].parent != layout::kNoNode) {
    const auto& parent_style = nodes[nodes[*id].parent].style;
    monotonic_forward = !parent_style.wrap && (parent_style.direction == layout::Direction::kRow ||
                                               parent_style.direction == layout::Direction::kColumn);
  }

  // When drawing a `for` comprehension, run two passes (all row boxes, then all row text)
  // so every row's background batches into 1 draw call and every row's text batches into 1 draw call.
  if (draw && node->GetExpr()->IsListComprehension()) {
    for (int pass : {1, 2}) {
      FlowGraphBuilder::HoleScope scope{
          plan_, *id, true, static_cast<PaintPass>(pass), CurrentClip(), nullptr, monotonic_forward};
      owner->hole_ = &scope;
      host_->Emit(LayoutSlotBeginInstr::New(plan_, *id));
      NodeVisitor for_expr(owner);
      const bool ok = node->GetExpr()->Accept(&for_expr);
      owner->hole_ = outer;
      if (!ok)
        return false;
    }
    return true;
  }

  FlowGraphBuilder::HoleScope scope{
      plan_, *id, draw, static_cast<PaintPass>(0), CurrentClip(), nullptr, monotonic_forward};
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
