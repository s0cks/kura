#ifndef KURA_GL_FIXTURE_H
#define KURA_GL_FIXTURE_H

// clang-format off
#include <epoxy/gl.h>
#include <epoxy/egl.h>
// clang-format on

#include <benchmark/benchmark.h>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>

#include "backend/program.h"
#include "backend/runtime_abi.h"
#include "backend/session.h"
#include "heap/heap.h"
#include "renderer/gl/renderer.h"

namespace kura::test {

struct KuraBenchHarness {
  std::unique_ptr<kura::heap::Heap> heap;
  std::unique_ptr<kura::Program> program;
  std::unique_ptr<kura::Session> session;
  KuraRenderer renderer{};
  bool renderer_initialized = false;

  bool Init(const std::string& kui_path, uint32_t width, uint32_t height, std::string* error) {
    heap = std::make_unique<kura::heap::Heap>(kura::heap::Heap::SmallOptions());
    kura::heap::Heap::SetCurrent(heap.get());

    program = kura::Program::Load(kui_path, kura::Program::Options{.optimize = true}, error);
    if (!program) {
      return false;
    }

    session = kura::Session::Create(program.get(), error);
    if (!session) {
      return false;
    }

    if (!session->Send(kura::rt::MakeVariant("Reset"), error)) {
      return false;
    }

    KuraGlOptions gl_options{};
    gl_options.struct_size = sizeof(gl_options);
    gl_options.clear_on_begin = 1;
    gl_options.clear = KuraColor{0.08f, 0.09f, 0.11f, 1.0f};

    if (kura_gl_renderer_create(&gl_options, &renderer) != KURA_RENDERER_OK) {
      if (error)
        *error = "kura_gl_renderer_create failed";
      return false;
    }
    renderer_initialized = true;

    if (kura_rt_set_renderer(&renderer) != KURA_RENDERER_OK) {
      if (error)
        *error = "kura_rt_set_renderer failed";
      return false;
    }

    kura_rt_set_viewport(width, height);
    if (!session->Resized(static_cast<int>(width), static_cast<int>(height), error)) {
      return false;
    }
    if (!session->Focused(true, error)) {
      return false;
    }
    return true;
  }

  ~KuraBenchHarness() {
    if (renderer_initialized) {
      kura_rt_set_renderer(nullptr);
      if (renderer.ops && renderer.ops->destroy) {
        renderer.ops->destroy(renderer.self);
      }
    }
  }
};

class OpenGLFixture : public benchmark::Fixture {
 protected:
  GLsizei width_ = 1024;
  GLsizei height_ = 1024;

  EGLDisplay display_ = EGL_NO_DISPLAY;
  EGLContext context_ = EGL_NO_CONTEXT;
  GLuint fbo_ = 0;
  GLuint color_tex_ = 0;
  GLuint rbo_ = 0;
  GLuint time_query_ = 0;

 public:
  auto fbo() const -> GLuint {
    return fbo_;
  }

  auto time_query() const -> GLuint {
    return time_query_;
  }

 public:
  OpenGLFixture() = default;
  ~OpenGLFixture() override = default;

  void SetUp(const benchmark::State& state) override {
    if (state.range(0) > 0 && state.range(1) > 0) {
      width_ = static_cast<GLsizei>(state.range(0));
      height_ = static_cast<GLsizei>(state.range(1));
    } else {
      width_ = 1024;
      height_ = 1024;
    }

    if (epoxy_has_egl_extension(EGL_NO_DISPLAY, "EGL_MESA_platform_surfaceless")) {
      display_ = eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    } else {
      display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    }

    if (display_ == EGL_NO_DISPLAY || !eglInitialize(display_, nullptr, nullptr)) {
      std::abort();
    }

    if (!eglBindAPI(EGL_OPENGL_API)) {
      std::abort();
    }

    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE,
    };
    EGLConfig config = nullptr;
    EGLint num_configs = 0;
    if (!eglChooseConfig(display_, config_attribs, &config, 1, &num_configs) || num_configs == 0) {
      std::abort();
    }

    const EGLint ctx_attribs[] = {
        EGL_CONTEXT_MAJOR_VERSION,           3,        EGL_CONTEXT_MINOR_VERSION, 3, EGL_CONTEXT_OPENGL_PROFILE_MASK,
        EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE,
    };
    context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, ctx_attribs);
    if (context_ == EGL_NO_CONTEXT) {
      std::abort();
    }

    if (!eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, context_)) {
      std::abort();
    }

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    glGenTextures(1, &color_tex_);
    glBindTexture(GL_TEXTURE_2D, color_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_tex_, 0);

    glGenRenderbuffers(1, &rbo_);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width_, height_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo_);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      std::abort();
    }

    glViewport(0, 0, width_, height_);
    glGenQueries(1, &time_query_);
  }

  void TearDown(const benchmark::State& state) override {
    if (display_ != EGL_NO_DISPLAY && context_ != EGL_NO_CONTEXT) {
      eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, context_);

      if (time_query_ != 0) {
        glDeleteQueries(1, &time_query_);
        time_query_ = 0;
      }
      if (rbo_ != 0) {
        glDeleteRenderbuffers(1, &rbo_);
        rbo_ = 0;
      }
      if (fbo_ != 0) {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
      }
      if (color_tex_ != 0) {
        glDeleteTextures(1, &color_tex_);
        color_tex_ = 0;
      }

      eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
      eglDestroyContext(display_, context_);
      context_ = EGL_NO_CONTEXT;
    }

    if (display_ != EGL_NO_DISPLAY) {
      eglTerminate(display_);
      display_ = EGL_NO_DISPLAY;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }

  auto width() const -> GLsizei {
    return width_;
  }

  auto height() const -> GLsizei {
    return height_;
  }
};

}  // namespace kura::test

#endif  // KURA_GL_FIXTURE_H
