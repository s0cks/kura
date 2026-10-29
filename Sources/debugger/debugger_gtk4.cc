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
}

void Debugger::OnUnrealize(GtkGLArea* area, gpointer data) {
  gtk_gl_area_make_current(area);
}

class RenderScope {
 public:
  explicit RenderScope() {
    glViewport(0, 0, 720, 520);
    glDisable(GL_SCISSOR_TEST);

    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
  }
  ~RenderScope() {
    glUseProgram(0);
    glDisable(GL_SCISSOR_TEST);
  }
};

auto Debugger::OnRender(GtkGLArea* area, GdkGLContext* ctx, gpointer data) -> gboolean {
  int w = gtk_widget_get_width(GTK_WIDGET(area));
  int h = gtk_widget_get_height(GTK_WIDGET(area));
  RenderScope scope{};
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
  return g_application_run(G_APPLICATION(GetApp()), argc, argv);
}
}  // namespace kura
