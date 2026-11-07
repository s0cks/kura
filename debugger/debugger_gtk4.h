#ifndef KURA_DEBUGGER_GTK4_H
#define KURA_DEBUGGER_GTK4_H

#ifndef KURA_DEBUGGER_H
#error "Please #include<kura/debugger/debugger.h> instead."
#endif  // KURA_DEBUGGER_H

#include <gdk/gdk.h>
#include <gtk/gtk.h>

namespace kura {
class Debugger {
 private:
  GtkApplication* app_;

  static void OnActivate(GtkApplication* app, gpointer data);
  static void OnRealize(GtkGLArea* area, gpointer data);
  static void OnUnrealize(GtkGLArea* area, gpointer data);
  static auto OnRender(GtkGLArea* area, GdkGLContext* ctx, gpointer data) -> gboolean;

 public:
  Debugger();
  ~Debugger();

  auto GetApp() const -> GtkApplication* {
    return app_;
  }

  auto Run(int argc, char** argv) -> int;

  auto operator()(int argc, char** argv) -> int {
    return Run(argc, argv);
  }
};

}  // namespace kura

#endif  // KURA_DEBUGGER_GTK4_H
