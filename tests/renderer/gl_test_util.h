#ifndef KURA_TESTS_GL_TEST_UTIL_H
#define KURA_TESTS_GL_TEST_UTIL_H

// A surfaceless EGL context (Mesa's software rasterizer is enough, so no window or display is needed)
// that draws into an offscreen framebuffer, and a way to read what was drawn back, for the tests of
// the OpenGL renderer.
#include <epoxy/egl.h>
#include <epoxy/gl.h>

#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {
constexpr uint32_t kSize = 128;
// The offscreen framebuffer; a frame is drawn into the bottom-left corner of it, whatever its size.
constexpr uint32_t kFboSize = 1024;

struct Pixel {
  uint8_t r, g, b, a;
  auto operator==(const Pixel& rhs) const -> bool {
    return r == rhs.r && g == rhs.g && b == rhs.b && a == rhs.a;
  }
};

auto Near(const Pixel& a, const Pixel& b, const int tolerance = 1) -> bool {
  return std::abs(a.r - b.r) <= tolerance && std::abs(a.g - b.g) <= tolerance && std::abs(a.b - b.b) <= tolerance &&
         std::abs(a.a - b.a) <= tolerance;
}

constexpr Pixel kBlack = {0, 0, 0, 255};
constexpr Pixel kRed = {255, 0, 0, 255};
constexpr Pixel kBlue = {0, 0, 255, 255};

// One EGL context, made current on the main thread for the whole run, drawing into an FBO.
class GlContext {
 public:
  GlContext() {
    setenv("EGL_PLATFORM", "surfaceless", 0);
    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0;
    EGLint minor = 0;
    if (display_ == EGL_NO_DISPLAY || !eglInitialize(display_, &major, &minor) || !eglBindAPI(EGL_OPENGL_API))
      return;

    const EGLint config_attribs[] = {EGL_SURFACE_TYPE, 0, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE};
    EGLConfig config;
    EGLint count = 0;
    if (!eglChooseConfig(display_, config_attribs, &config, 1, &count) || count == 0)
      return;

    const EGLint context_attribs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
                                      EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
    context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, context_attribs);
    if (context_ == EGL_NO_CONTEXT || !eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, context_))
      return;

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glGenRenderbuffers(1, &rbo_);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, kFboSize, kFboSize);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo_);
    ok_ = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  }

  auto ok() const -> bool {
    return ok_;
  }

 private:
  EGLDisplay display_ = EGL_NO_DISPLAY;
  EGLContext context_ = EGL_NO_CONTEXT;
  GLuint fbo_ = 0;
  GLuint rbo_ = 0;
  bool ok_ = false;
};

auto Context() -> GlContext& {
  static GlContext context;
  return context;
}

// The framebuffer, addressed from the top-left like Kura's coordinates (GL's rows start at the bottom).
class Frame {
 public:
  // The `size` x `size` frame that was just drawn.
  explicit Frame(const uint32_t size = kSize) :
    size_(size),
    pixels_(static_cast<size_t>(size) * size) {
    glReadPixels(0, 0, static_cast<GLsizei>(size), static_cast<GLsizei>(size), GL_RGBA, GL_UNSIGNED_BYTE,
                 pixels_.data());
  }

  auto At(const uint32_t x, const uint32_t y) const -> Pixel {
    return pixels_[static_cast<size_t>(size_ - 1 - y) * size_ + x];
  }

  auto Count(const Pixel& color) const -> size_t {
    size_t count = 0;
    for (const auto& pixel : pixels_)
      count += pixel == color ? 1 : 0;
    return count;
  }

 private:
  uint32_t size_;
  std::vector<Pixel> pixels_;
};

}  // namespace

#endif  // KURA_TESTS_GL_TEST_UTIL_H
