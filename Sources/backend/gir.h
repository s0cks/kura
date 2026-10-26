#ifndef KURA_GIR_H
#define KURA_GIR_H

#include "ir.h"

namespace kura {
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

  DECLARE_INSTRUCTION_TYPE(BeginFrame);

 public:
  static inline auto New(const uint32_t width, const uint32_t height) -> BeginFrameInstr* {
    return new BeginFrameInstr(width, height);
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

class FillRectInstr : public GraphicsInstr {
 private:
 public:
  explicit FillRectInstr() = default;
  ~FillRectInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(FillRect);

 public:
  static inline auto New() -> FillRectInstr* {
    return new FillRectInstr();
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

class DrawTextInstr : public GraphicsInstr {
 private:
  Value* value_;

 public:
  explicit DrawTextInstr(Value* value) :
    value_(value) {}
  ~DrawTextInstr() override = default;

  auto GetValue() const -> Value* {
    return value_;
  }

  DECLARE_INSTRUCTION_TYPE(DrawText);

 public:
  static inline auto New(Value* value) -> DrawTextInstr* {
    return new DrawTextInstr(value);
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

class DrawElipseInstr : public GraphicsInstr {
 public:
  DrawElipseInstr() = default;
  ~DrawElipseInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(DrawElipse);

 public:
  static inline auto New() -> DrawElipseInstr* {
    return new DrawElipseInstr();
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
}  // namespace kura

#endif  // KURA_GIR_H
