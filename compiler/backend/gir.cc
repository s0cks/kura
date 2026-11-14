#include "gir.h"

namespace kura {
// TODO(@s0cks): duplicate of ir.cc
static inline auto Name(const Value* value) -> std::string {
  if (!value || !value->GetDefinition())
    return "<none>";

  return "%" + std::to_string(value->GetDefinition()->GetId());
}

auto CheckRendererStatusInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "CheckRendererStatus(";
  ss << Name(GetValue());
  ss << ")";
  return ss.str();
}

auto BeginFrameInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "BeginFrame(";
  if (UsesViewport()) {
    ss << "viewport";
  } else {
    ss << "width=" << GetWidth() << ", ";
    ss << "height=" << GetHeight();
  }
  ss << ")";
  return ss.str();
}

auto LayoutSetTextInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "LayoutSetText(";
  ss << "layout=" << GetPlan()->GetIndex() << ", ";
  ss << "node=" << GetNode() << ", ";
  ss << "text=" << Name(GetText()) << ", ";
  ss << "size=" << GetFontSize();
  if (!GetFontFamily().empty())
    ss << ", family=\"" << GetFontFamily() << "\"";
  ss << ")";
  return ss.str();
}

auto LayoutSetPropertyInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "LayoutSetProperty(";
  ss << "layout=" << GetPlan()->GetIndex() << ", ";
  ss << "node=" << GetNode() << ", ";
  ss << "property=" << static_cast<uint64_t>(GetProperty()) << ", ";
  ss << "value=" << Name(GetValue());
  ss << ")";
  return ss.str();
}

auto LayoutRunInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "LayoutRun(layout=" << GetPlan()->GetIndex() << ", nodes=" << GetPlan()->GetNumberOfNodes() << ")";
  return ss.str();
}

auto LayoutSlotBeginInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "LayoutSlotBegin(layout=" << GetPlan()->GetIndex() << ", slot=" << GetSlot() << ")";
  return ss.str();
}

auto LayoutSlotInstanceInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "LayoutSlotInstance(layout=" << GetPlan()->GetIndex() << ", slot=" << GetSlot()
     << ", template=" << GetTemplate()->GetIndex() << ", nodes=" << GetTemplate()->GetNumberOfNodes() << ")";
  return ss.str();
}

auto LayoutSlotEndInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "LayoutSlotEnd(layout=" << GetPlan()->GetIndex() << ", slot=" << GetSlot() << ")";
  return ss.str();
}

auto LayoutBoxInstr::ToString() const -> std::string {
  static constexpr const char* kNames[] = {"x", "y", "width", "height"};
  std::stringstream ss{};
  ss << "LayoutBox(";
  ss << "layout=" << GetPlan()->GetIndex() << ", ";
  ss << "node=" << GetNode() << ", ";
  ss << kNames[GetComponent()];
  ss << ")";
  return ss.str();
}

auto EndFrameInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "EndFrame(";
  ss << ")";
  return ss.str();
}

auto FillRectInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "FillRect(";
  ss << "x=" << Name(GetX()) << ", ";
  ss << "y=" << Name(GetY()) << ", ";
  ss << "width=" << Name(GetWidth()) << ", ";
  ss << "height=" << Name(GetHeight()) << ", ";
  ss << "color=" << Name(GetColor());
  ss << ")";
  return ss.str();
}

auto DrawRectInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "DrawRect(";
  ss << ")";
  return ss.str();
}

auto RegisterEventInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "RegisterEvent(";
  ss << "layout=" << GetPlan()->GetIndex() << ", ";
  ss << "node=" << GetNode() << ", ";
  ss << "kind=" << GetKind() << ", ";
  ss << "message=" << Name(GetMessage());
  ss << ")";
  return ss.str();
}

auto DrawTextInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "DrawText(";
  ss << "layout=" << GetPlan()->GetIndex() << ", ";
  ss << "node=" << GetNode() << ", ";
  ss << "text=" << Name(GetValue()) << ", ";
  ss << "color=" << Name(GetColor());
  ss << ")";
  return ss.str();
}

auto PushClipInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "PushClip(";
  ss << ")";
  return ss.str();
}

auto PopClipInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "PopClip(";
  ss << ")";
  return ss.str();
}

auto FillRoundRectInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "FillRoundRect(";
  ss << ")";
  return ss.str();
}

auto FillCircleInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "FillCircle(";
  ss << ")";
  return ss.str();
}

auto FillPolygonInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "FillPolygon(";
  ss << ")";
  return ss.str();
}

auto FillBezierInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "FillBezier(";
  ss << ")";
  return ss.str();
}

auto DrawLineInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "DrawLine(";
  ss << ")";
  return ss.str();
}

auto DrawRoundRectInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "DrawRoundRect(";
  ss << ")";
  return ss.str();
}

auto DrawCircleInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "DrawCircle(";
  ss << ")";
  return ss.str();
}

auto DrawEllipseInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "DrawEllipse(";
  ss << ")";
  return ss.str();
}

auto DrawPolygonInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "DrawPolygon(";
  ss << ")";
  return ss.str();
}

auto DrawStrokeBezierInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "DrawStrokeBezier(";
  ss << ")";
  return ss.str();
}

auto DrawQuadBezierInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "DrawQuadBezier(";
  ss << ")";
  return ss.str();
}

auto MeasureTextInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "MeasureText(";
  ss << ")";
  return ss.str();
}
}  // namespace kura
