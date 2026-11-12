#ifndef KURA_GIR_H
#define KURA_GIR_H

#include <memory>

#include "backend/layout_plan.h"
#include "ir.h"

namespace kura {
class TracyBeginZoneInstr : public EntryInstr {
 public:
  TracyBeginZoneInstr() {}
  ~TracyBeginZoneInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(TracyBeginZone);

 public:
  static inline auto New() -> TracyBeginZoneInstr* {
    return new TracyBeginZoneInstr();
  }
};

class TracyEndZoneInstr : public Instruction {
 public:
  TracyEndZoneInstr() = default;
  ~TracyEndZoneInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(TracyEndZone);

 public:
  static inline auto New() -> TracyEndZoneInstr* {
    return new TracyEndZoneInstr();
  }
};

class CheckRendererStatusInstr : public Instruction {
 private:
  Value* value_;

 public:
  CheckRendererStatusInstr(Value* value) :
    value_(value) {}
  ~CheckRendererStatusInstr() override = default;

  auto GetValue() const -> Value* {
    return value_;
  }

  DECLARE_INSTRUCTION_TYPE(CheckRendererStatus);

 public:
  static inline auto New(Value* defn) -> CheckRendererStatusInstr* {
    return new CheckRendererStatusInstr(defn);
  }
};

class GraphicsInstr : public Definition {
 public:
  GraphicsInstr() = default;
  ~GraphicsInstr() override = default;

  auto IsGraphicsInstr() const -> bool override {
    return true;
  }
};

template <const uint64_t NumberOfInputs>
class TemplateGraphicsInstr : public TemplateDefinition<NumberOfInputs> {
 public:
  TemplateGraphicsInstr() = default;
  ~TemplateGraphicsInstr() override = default;

  auto IsGraphicsInstr() const -> bool override {
    return true;
  }
};

class BeginFrameInstr : public GraphicsInstr {
 private:
  uint32_t width_;
  uint32_t height_;

 public:
  BeginFrameInstr(const uint32_t width, const uint32_t height) :
    GraphicsInstr(),
    width_(width),
    height_(height) {}
  ~BeginFrameInstr() override = default;

  auto GetWidth() const -> uint32_t {
    return width_;
  }

  auto GetHeight() const -> uint32_t {
    return height_;
  }

  // A frame as big as the viewport the host set (kura_rt_set_viewport), which is read when the frame
  // begins. It is the frame of a view: the window can change size between two of them.
  auto UsesViewport() const -> bool {
    return width_ == 0 && height_ == 0;
  }

  DECLARE_INSTRUCTION_TYPE(BeginFrame);

 public:
  static inline auto New(const uint32_t width, const uint32_t height) -> BeginFrameInstr* {
    return new BeginFrameInstr(width, height);
  }

  static inline auto New() -> BeginFrameInstr* {
    return new BeginFrameInstr(0, 0);
  }
};

class EndFrameInstr : public GraphicsInstr {
 private:
 public:
  EndFrameInstr() = default;
  ~EndFrameInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(EndFrame);

 public:
  static inline auto New() -> EndFrameInstr* {
    return new EndFrameInstr();
  }
};

class FillRectInstr : public TemplateGraphicsInstr<5> {
 public:
  static const auto kXPos = 0;
  static const auto kYPos = 1;
  static const auto kWidthPos = 2;
  static const auto kHeightPos = 3;
  static const auto kColorPos = 4;

 public:
  FillRectInstr(Value* x, Value* y, Value* w, Value* h, Value* c) {
    SetX(x);
    SetY(y);
    SetWidth(w);
    SetHeight(h);
    SetColor(c);
  }
  ~FillRectInstr() override = default;

  inline auto GetX() const -> Value* {
    return GetInputAt(kXPos);
  }

  inline void SetX(Value* rhs) {
    return SetInputAt(kXPos, rhs);
  }

  inline auto GetY() const -> Value* {
    return GetInputAt(kYPos);
  }

  inline void SetY(Value* rhs) {
    return SetInputAt(kYPos, rhs);
  }

  inline auto GetWidth() const -> Value* {
    return GetInputAt(kWidthPos);
  }

  inline void SetWidth(Value* rhs) {
    return SetInputAt(kWidthPos, rhs);
  }

  inline auto GetHeight() const -> Value* {
    return GetInputAt(kHeightPos);
  }

  inline void SetHeight(Value* rhs) {
    return SetInputAt(kHeightPos, rhs);
  }

  inline auto GetColor() const -> Value* {
    return GetInputAt(kColorPos);
  }

  inline void SetColor(Value* rhs) {
    return SetInputAt(kColorPos, rhs);
  }

  DECLARE_INSTRUCTION_TYPE(FillRect);

 public:
  static inline auto New(Value* x, Value* y, Value* w, Value* h, Value* color) -> FillRectInstr* {
    return new FillRectInstr(x, y, w, h, color);
  }
};

class DrawRectInstr : public GraphicsInstr {
 private:
 public:
  explicit DrawRectInstr() = default;
  ~DrawRectInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(DrawRect);

 public:
  static inline auto New() -> DrawRectInstr* {
    return new DrawRectInstr();
  }
};

// Draws a text node. What is drawn, where, and at what size and in which family is the layout's: it has the
// text, its box and its font, so the instruction only names the node, and the color is the one thing it's
// given. The value is the text's content, kept as an input so that it is computed before the draw.
class DrawTextInstr : public TemplateGraphicsInstr<2> {
 public:
  static const auto kValuePos = 0;
  static const auto kColorPos = 1;

 private:
  std::shared_ptr<LayoutPlan> plan_;
  layout::NodeId node_;

 public:
  DrawTextInstr(std::shared_ptr<LayoutPlan> plan, const layout::NodeId node, Value* value, Value* color) :
    plan_(std::move(plan)),
    node_(node) {
    SetInputAt(kValuePos, value);
    SetInputAt(kColorPos, color);
  }
  ~DrawTextInstr() override = default;

  inline auto GetPlan() const -> const std::shared_ptr<LayoutPlan>& {
    return plan_;
  }

  inline auto GetNode() const -> layout::NodeId {
    return node_;
  }

  inline auto GetValue() const -> Value* {
    return GetInputAt(kValuePos);
  }

  inline auto GetColor() const -> Value* {
    return GetInputAt(kColorPos);
  }

  DECLARE_INSTRUCTION_TYPE(DrawText);

 public:
  static inline auto New(std::shared_ptr<LayoutPlan> plan, const layout::NodeId node, Value* value, Value* color)
      -> DrawTextInstr* {
    return new DrawTextInstr(std::move(plan), node, value, color);
  }
};

// Says that the box of a node means a Message when an event of `kind` (`rt::EventKind`) happens in it. It is
// emitted where the node is drawn, after layout, so the box is the one that was drawn, and the runtime keeps
// it for the frame for a host to look up.
class RegisterEventInstr : public TemplateGraphicsInstr<1> {
 public:
  static const auto kMessagePos = 0;

 private:
  std::shared_ptr<LayoutPlan> plan_;
  layout::NodeId node_;
  uint64_t kind_;

 public:
  RegisterEventInstr(std::shared_ptr<LayoutPlan> plan, const layout::NodeId node, const uint64_t kind,
                     Value* message) :
    plan_(std::move(plan)),
    node_(node),
    kind_(kind) {
    SetInputAt(kMessagePos, message);
  }
  ~RegisterEventInstr() override = default;

  inline auto GetPlan() const -> const std::shared_ptr<LayoutPlan>& {
    return plan_;
  }

  inline auto GetNode() const -> layout::NodeId {
    return node_;
  }

  inline auto GetKind() const -> uint64_t {
    return kind_;
  }

  inline auto GetMessage() const -> Value* {
    return GetInputAt(kMessagePos);
  }

  DECLARE_INSTRUCTION_TYPE(RegisterEvent);

 public:
  static inline auto New(std::shared_ptr<LayoutPlan> plan, const layout::NodeId node, const uint64_t kind,
                         Value* message) -> RegisterEventInstr* {
    return new RegisterEventInstr(std::move(plan), node, kind, message);
  }
};

class PushClipInstr : public GraphicsInstr {
 public:
  PushClipInstr() = default;
  ~PushClipInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(PushClip);

 public:
  static inline auto New() -> PushClipInstr* {
    return new PushClipInstr();
  }
};

class PopClipInstr : public GraphicsInstr {
 public:
  PopClipInstr() = default;
  ~PopClipInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(PopClip);

 public:
  static inline auto New() -> PopClipInstr* {
    return new PopClipInstr();
  }
};

class FillRoundRectInstr : public GraphicsInstr {
 public:
  FillRoundRectInstr() = default;
  ~FillRoundRectInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(FillRoundRect);

 public:
  static inline auto New() -> FillRoundRectInstr* {
    return new FillRoundRectInstr();
  }
};

class FillCircleInstr : public GraphicsInstr {
 public:
  FillCircleInstr() = default;
  ~FillCircleInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(FillCircle);

 public:
  static inline auto New() -> FillCircleInstr* {
    return new FillCircleInstr();
  }
};

class FillPolygonInstr : public GraphicsInstr {
 public:
  FillPolygonInstr() = default;
  ~FillPolygonInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(FillPolygon);

 public:
  static inline auto New() -> FillPolygonInstr* {
    return new FillPolygonInstr();
  }
};

class FillBezierInstr : public GraphicsInstr {
 public:
  FillBezierInstr() = default;
  ~FillBezierInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(FillBezier);

 public:
  static inline auto New() -> FillBezierInstr* {
    return new FillBezierInstr();
  }
};

class DrawLineInstr : public GraphicsInstr {
 public:
  DrawLineInstr() = default;
  ~DrawLineInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(DrawLine);

 public:
  static inline auto New() -> DrawLineInstr* {
    return new DrawLineInstr();
  }
};

class DrawRoundRectInstr : public GraphicsInstr {
 public:
  DrawRoundRectInstr() = default;
  ~DrawRoundRectInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(DrawRoundRect);

 public:
  static inline auto New() -> DrawRoundRectInstr* {
    return new DrawRoundRectInstr();
  }
};

class DrawCircleInstr : public GraphicsInstr {
 public:
  DrawCircleInstr() = default;
  ~DrawCircleInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(DrawCircle);

 public:
  static inline auto New() -> DrawCircleInstr* {
    return new DrawCircleInstr();
  }
};

class DrawEllipseInstr : public GraphicsInstr {
 public:
  DrawEllipseInstr() = default;
  ~DrawEllipseInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(DrawEllipse);

 public:
  static inline auto New() -> DrawEllipseInstr* {
    return new DrawEllipseInstr();
  }
};

class DrawPolygonInstr : public GraphicsInstr {
 public:
  DrawPolygonInstr() = default;
  ~DrawPolygonInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(DrawPolygon);

 public:
  static inline auto New() -> DrawPolygonInstr* {
    return new DrawPolygonInstr();
  }
};

class DrawStrokeBezierInstr : public GraphicsInstr {
 public:
  DrawStrokeBezierInstr() = default;
  ~DrawStrokeBezierInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(DrawStrokeBezier);

 public:
  static inline auto New() -> DrawStrokeBezierInstr* {
    return new DrawStrokeBezierInstr();
  }
};

class DrawQuadBezierInstr : public GraphicsInstr {
 public:
  DrawQuadBezierInstr() = default;
  ~DrawQuadBezierInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(DrawQuadBezier);

 public:
  static inline auto New() -> DrawQuadBezierInstr* {
    return new DrawQuadBezierInstr();
  }
};

class MeasureTextInstr : public GraphicsInstr {
 public:
  MeasureTextInstr() = default;
  ~MeasureTextInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(MeasureText);

 public:
  static inline auto New() -> MeasureTextInstr* {
    return new MeasureTextInstr();
  }
};

// Gives a text node of a layout plan its content, which layout has to measure.
class LayoutSetTextInstr : public TemplateGraphicsInstr<1> {
 private:
  std::shared_ptr<LayoutPlan> plan_;
  layout::NodeId node_;
  double font_size_;
  std::string font_family_;

 public:
  LayoutSetTextInstr(std::shared_ptr<LayoutPlan> plan, const layout::NodeId node, Value* text, const double font_size,
                     std::string font_family) :
    plan_(std::move(plan)),
    node_(node),
    font_size_(font_size),
    font_family_(std::move(font_family)) {
    SetInputAt(0, text);
  }
  ~LayoutSetTextInstr() override = default;

  inline auto GetPlan() const -> const std::shared_ptr<LayoutPlan>& {
    return plan_;
  }

  inline auto GetNode() const -> layout::NodeId {
    return node_;
  }

  inline auto GetText() const -> Value* {
    return GetInputAt(0);
  }

  inline auto GetFontSize() const -> double {
    return font_size_;
  }

  inline auto GetFontFamily() const -> const std::string& {
    return font_family_;
  }

  DECLARE_INSTRUCTION_TYPE(LayoutSetText);

 public:
  static inline auto New(std::shared_ptr<LayoutPlan> plan, const layout::NodeId node, Value* text,
                         const double font_size, std::string font_family = {}) -> LayoutSetTextInstr* {
    return new LayoutSetTextInstr(std::move(plan), node, text, font_size, std::move(font_family));
  }
};

// Sets a layout property of a node to a value that is only known at run time. The value is whatever the
// property accepts (`Px`, `Pct`, `Fr`, a number, a string, a bool): the runtime reads it by its tag.
class LayoutSetPropertyInstr : public TemplateGraphicsInstr<1> {
 private:
  std::shared_ptr<LayoutPlan> plan_;
  layout::NodeId node_;
  layout::Property property_;

 public:
  LayoutSetPropertyInstr(std::shared_ptr<LayoutPlan> plan, const layout::NodeId node, const layout::Property property,
                         Value* value) :
    plan_(std::move(plan)),
    node_(node),
    property_(property) {
    SetInputAt(0, value);
  }
  ~LayoutSetPropertyInstr() override = default;

  inline auto GetPlan() const -> const std::shared_ptr<LayoutPlan>& {
    return plan_;
  }

  inline auto GetNode() const -> layout::NodeId {
    return node_;
  }

  inline auto GetProperty() const -> layout::Property {
    return property_;
  }

  inline auto GetValue() const -> Value* {
    return GetInputAt(0);
  }

  DECLARE_INSTRUCTION_TYPE(LayoutSetProperty);

 public:
  static inline auto New(std::shared_ptr<LayoutPlan> plan, const layout::NodeId node, const layout::Property property,
                         Value* value) -> LayoutSetPropertyInstr* {
    return new LayoutSetPropertyInstr(std::move(plan), node, property, value);
  }
};

// Lays a plan's tree out, if what it depends on changed since the last time.
class LayoutRunInstr : public GraphicsInstr {
 private:
  std::shared_ptr<LayoutPlan> plan_;

 public:
  explicit LayoutRunInstr(std::shared_ptr<LayoutPlan> plan) :
    GraphicsInstr(),
    plan_(std::move(plan)) {}
  ~LayoutRunInstr() override = default;

  inline auto GetPlan() const -> const std::shared_ptr<LayoutPlan>& {
    return plan_;
  }

  DECLARE_INSTRUCTION_TYPE(LayoutRun);

 public:
  static inline auto New(std::shared_ptr<LayoutPlan> plan) -> LayoutRunInstr* {
    return new LayoutRunInstr(std::move(plan));
  }
};

// One coordinate of a node's laid out box: a Px, in the viewport's coordinates.
class LayoutBoxInstr : public GraphicsInstr {
 public:
  enum Component : uint64_t {
    kX = 0,
    kY = 1,
    kWidth = 2,
    kHeight = 3,
  };

 private:
  std::shared_ptr<LayoutPlan> plan_;
  layout::NodeId node_;
  Component component_;

 public:
  LayoutBoxInstr(std::shared_ptr<LayoutPlan> plan, const layout::NodeId node, const Component component) :
    GraphicsInstr(),
    plan_(std::move(plan)),
    node_(node),
    component_(component) {}
  ~LayoutBoxInstr() override = default;

  inline auto GetPlan() const -> const std::shared_ptr<LayoutPlan>& {
    return plan_;
  }

  inline auto GetNode() const -> layout::NodeId {
    return node_;
  }

  inline auto GetComponent() const -> Component {
    return component_;
  }

  DECLARE_INSTRUCTION_TYPE(LayoutBox);

 public:
  static inline auto New(std::shared_ptr<LayoutPlan> plan, const layout::NodeId node, const Component component)
      -> LayoutBoxInstr* {
    return new LayoutBoxInstr(std::move(plan), node, component);
  }
};
}  // namespace kura

#endif  // KURA_GIR_H
