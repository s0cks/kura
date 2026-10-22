#include <epoxy/egl.h>
#include <stddef.h>
#include <string.h>

#include "debugger/debugger.h"
#include "renderer/renderer.h"

namespace kura {
Debugger::Debugger() {
  app_ = gtk_application_new("com.example.gl_renderer", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(GetApp(), "activate", G_CALLBACK(OnActivate), NULL);
}

Debugger::~Debugger() {
  g_object_unref(GetApp());
}

void Debugger::OnRealize(GtkGLArea* area, gpointer data) {
  gtk_gl_area_make_current(area);
  if (gtk_gl_area_get_error(area) != NULL)
    return;
  Renderer::Init();
}

void Debugger::OnUnrealize(GtkGLArea* area, gpointer data) {
  gtk_gl_area_make_current(area);
  Renderer::DeInit();
}

static constexpr const Color kClearColor = {
    0.12f,
    0.12f,
    0.14f,
    1.0f,
};

class RenderScope {
 private:
  Renderer* owner_;

 public:
  explicit RenderScope(Renderer* owner) :
    owner_(owner) {
    const auto& resolution = GetOwner()->GetResolution();
    glViewport(0, 0, resolution[0], resolution[1]);
    glDisable(GL_SCISSOR_TEST);

    glClearColor(kClearColor[0], kClearColor[1], kClearColor[2], kClearColor[3]);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(GetOwner()->GetProgram());
    glUniform2f(GetOwner()->GetResolutionLoc(), resolution[0], resolution[1]);
  }
  ~RenderScope() {
    GetOwner()->GetContext()->Flush();
    glUseProgram(0);
    glDisable(GL_SCISSOR_TEST);
  }

  inline auto GetOwner() const -> Renderer* {
    return owner_;
  }

  auto operator->() const -> const std::unique_ptr<RenderContext>& {
    return GetOwner()->GetContext();
  }
};

auto Debugger::OnRender(GtkGLArea* area, GdkGLContext* ctx, gpointer data) -> gboolean {
  int w = gtk_widget_get_width(GTK_WIDGET(area));
  int h = gtk_widget_get_height(GTK_WIDGET(area));
  RenderScope render(Renderer::Get());
  render->SetColor(glm::vec4(0.18f, 0.22f, 0.32f, 1.0f));
  render->FillRect(glm::vec2(10, 10), glm::vec2(w - 20, h - 20));

  float cx = 30, cy = 30, cw = w - 60, ch = h - 60;
  render->ClipRect(glm::vec2(cx, cy), glm::vec2(cw, ch));

  render->SetColor(glm::vec4(0.25f, 0.80f, 0.45f, 1.0f));
  render->FillRect(glm::vec2(cx - 20, cy - 20), glm::vec2(180, 130));

  render->SetColor(glm::vec4(0.95f, 0.55f, 0.10f, 1.0f));
  render->FillRect(glm::vec2(w / 2.0f - 70, h / 2.0f - 45), glm::vec2(140, 90));

  render->SetColor(glm::vec4(0.40f, 0.55f, 0.95f, 1.0f));
  render->FillRect(glm::vec2(40, h - 130), glm::vec2(110, 70));

  render->SetColor(glm::vec4(1.0f, 1.0f, 1.0f, 0.85f));
  render->FillRect(glm::vec2(w / 2.0f - 70, h / 2.0f - 45), glm::vec2(140, 90));

  render->SetColor(glm::vec4(0.95f, 0.25f, 0.30f, 1.0f));
  render->DrawRect(glm::vec2(w - 90, 50), glm::vec2(120, 80));

  render->SetColor(glm::vec4(0.80f, 0.80f, 0.20f, 1.0f));
  render->DrawRect(glm::vec2(40, h - 130), glm::vec2(110, 70));

  // for (int i = 0; i < 6; i++) {
  //   float t = (float)i / 5.0f;
  //   float ly = h - 170 + i * 14;
  //   render_set_color(t, 0.6f, 1.0f - t, 0.75f);
  //   render_draw_line(50, ly, w - 50, ly);
  // }

  render->ClipReset();

  render->SetColor(glm::vec4(1.0f, 1.0f, 1.0f, 0.30f));
  render->DrawRect(glm::vec2(10, 10), glm::vec2(w - 20, h - 20));

  render->SetColor(glm::vec4(0.90f, 0.85f, 0.20f, 0.50f));
  render->DrawRect(glm::vec2(cx, cy), glm::vec2(cw, ch));
  return true;
}

void Debugger::OnActivate(GtkApplication* app, gpointer data) {
  GtkWidget* win = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(win), "Kura Debugger");
  gtk_window_set_default_size(GTK_WINDOW(win), 720, 520);

  GtkWidget* gl = gtk_gl_area_new();
  gtk_gl_area_set_required_version(GTK_GL_AREA(gl), 3, 3);
  g_signal_connect(gl, "realize", G_CALLBACK(OnRealize), NULL);
  g_signal_connect(gl, "unrealize", G_CALLBACK(OnUnrealize), NULL);
  g_signal_connect(gl, "render", G_CALLBACK(OnRender), NULL);

  gtk_window_set_child(GTK_WINDOW(win), gl);
  gtk_window_present(GTK_WINDOW(win));
}

auto Debugger::Run(int argc, char** argv) -> int {
  int status = g_application_run(G_APPLICATION(GetApp()), argc, argv);
  return status;
}
}  // namespace kura
