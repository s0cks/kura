#ifndef KURA_RENDERER_GL_H
#define KURA_RENDERER_GL_H

#ifndef KURA_RENDERER_H
#error "Please include <renderer/renderer.h> instead."
#endif  // KURA_RENDERER_H

#ifdef KURA_RENDER_TARGET_OPENGL

#include <epoxy/gl.h>
#include <glm/glm.hpp>
#include <memory>

#include "renderer/batch_gl.h"
#include "renderer/color.h"

namespace kura {
class Renderer;
class RenderContext {
  friend class Renderer;
  friend class RenderScope;

 private:
  Renderer* owner_;
  Color color_;
  std::unique_ptr<TriangleBatch> tris_;
  std::unique_ptr<LineBatch> lines_;

  inline auto GetTriangles() const -> const std::unique_ptr<TriangleBatch>& {
    return tris_;
  }

  inline auto GetLines() const -> const std::unique_ptr<LineBatch>& {
    return lines_;
  }

  void Flush();

 public:
  explicit RenderContext(Renderer* owner);
  ~RenderContext() = default;

  auto GetOwner() const -> Renderer* {
    return owner_;
  }

  void SetColor(const Color rhs) {
    color_ = std::move(rhs);
  }

  inline void SetColor(const glm::vec3 rhs) {
    return SetColor(Color(std::move(rhs), 1.0f));
  }

  inline void SetColor(const float r, const float g, const float b) {
    return SetColor(glm::vec3(r, g, b));
  }

  auto GetColor() const -> const Color& {
    return color_;
  }

  void ClipRect(const glm::vec2 pos, const glm::vec2 size);
  void ClipReset();

  void DrawRect(const glm::vec2 pos, const glm::vec2 size);
  void DrawLine(const glm::vec2 start, const glm::vec2 end);

  void FillRect(const glm::vec2 pos, const glm::vec2 size);
};

class Shader {
 public:
  using Handle = GLuint;

  enum Kind : GLenum {
    kFragment = GL_FRAGMENT_SHADER,
    kVertex = GL_VERTEX_SHADER,
  };

 private:
  Handle handle_;

 public:
  Shader(const Kind kind, const std::string_view source);
  ~Shader();

  auto GetHandle() const -> Handle {
    return handle_;
  }

  operator Handle() const {
    return handle_;
  }
};

class Renderer {
  friend class Debugger;

 private:
  glm::vec2 resolution_;
  GLuint program_;
  GLint loc_res_;
  std::unique_ptr<RenderContext> ctx_;

 public:
  explicit Renderer(const glm::vec2 resolution);
  ~Renderer();

  auto GetProgram() const -> GLuint {
    return program_;
  }

  auto GetResolutionLoc() const -> GLint {
    return loc_res_;
  }

  auto GetResolution() const -> const glm::vec2& {
    return resolution_;
  }

  auto GetContext() const -> const std::unique_ptr<RenderContext>& {
    return ctx_;
  }

 private:
  static void SetRenderer(Renderer* rhs);

 public:
  static void Init();
  static void DeInit();
  static auto Get() -> Renderer*;
};
}  // namespace kura

#endif  // KURA_RENDER_TARGET_OPENGL

#endif  // KURA_RENDERER_GL_H
