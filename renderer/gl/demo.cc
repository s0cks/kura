// Runs a Kura program in a window: the file is parsed, compiled and loaded into the JIT, and then its
// view is called once a frame. The compiled view begins the frame, draws, and ends it through the
// runtime ABI, so all the host does is own the window and the GL context, and give the runtime a
// renderer to draw with.
//
//   kura-gl-demo <file.kui> [args...] [--function <name>] [--frames <n>] [--no-opt]
//
// `args` are what the view is called with, written like they are for `kura --run`; if there are none,
// each parameter gets the zero of its type. A view draws into a 1024x1024 frame, so that's the window.
#include <GLFW/glfw3.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <string>
#include <vector>

#include "backend/program.h"
#include "backend/runtime_abi.h"
#include "backend/session.h"
#include "heap/heap.h"
#include "renderer/gl/renderer.h"

// TODO(@s0cks): move to common
#ifdef KURA_DEBUG

#define PROFILE_BEGIN(Name) const auto Name##_start = std::chrono::high_resolution_clock::now();
#define PROFILE_END(Name)                                                                                     \
  const auto Name##_end = std::chrono::high_resolution_clock::now();                                          \
  const auto Name##_dur = std::chrono::duration_cast<std::chrono::microseconds>((Name##_end - Name##_start)); \
  Name##_samples.push_back(Name##_dur.count());

#else

#define PROFILE_BEGIN(Name)
#define PROFILE_END(Name)

#endif  // KURA_DEBUG

namespace {
// The window starts this big, in screen points, and can be resized.
constexpr int kFrameSize = 1024;

struct Options {
  std::string file{};
  std::string function = "view";
  std::vector<std::string> args{};
  long frames = -1;
  bool optimize = true;
};

auto ParseOptions(const int argc, char** argv, Options* options) -> bool {
  for (int i = 1; i < argc; i++) {
    const std::string arg(argv[i]);
    if (arg == "--function" || arg == "--frames") {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "%s needs a value\n", arg.c_str());
        return false;
      }

      const std::string value(argv[++i]);
      if (arg == "--function")
        options->function = value;
      else
        options->frames = std::atol(value.c_str());
    } else if (arg == "--no-opt") {
      options->optimize = false;
    } else if (options->file.empty()) {
      options->file = arg;
    } else {
      options->args.push_back(arg);
    }
  }
  return !options->file.empty();
}

void OnGlfwError(const int code, const char* message) {
  std::fprintf(stderr, "glfw error %d: %s\n", code, message);
}

// Deadzone in logical window points before a touch/click becomes a scroll drag
constexpr double kDragThreshold = 8.0;

struct WindowState {
  bool focus_changed = false;
  bool focused = false;
  double wheel_x = 0;
  double wheel_y = 0;

  // Pointer / touch drag state
  bool pointer_down = false;
  bool is_dragging = false;
  double press_x = 0.0;
  double press_y = 0.0;
  double last_x = 0.0;
  double last_y = 0.0;
};

// How far a notch of the wheel scrolls, in pixels.
constexpr double kPixelsPerNotch = 40.0;

void OnScroll(GLFWwindow* window, const double x, const double y) {
  const auto state = static_cast<WindowState*>(glfwGetWindowUserPointer(window));
  state->wheel_x += x;
  state->wheel_y += y;
}

void OnFocus(GLFWwindow* window, const int focused) {
  const auto state = static_cast<WindowState*>(glfwGetWindowUserPointer(window));
  state->focus_changed = true;
  state->focused = focused != 0;
}
}  // namespace

int main(const int argc, char** argv) {
  using namespace kura;

  Options options{};
  if (!ParseOptions(argc, argv, &options)) {
    std::fprintf(stderr, "usage: kura-gl-demo <file.kui> [args...] [--function <name>] [--frames <n>] [--no-opt]\n");
    return EXIT_FAILURE;
  }

  const auto heap = new heap::Heap(heap::Heap::SmallOptions());
  KASSERT(heap);
  heap::Heap::SetCurrent(heap);

  // compile before opening a window, so a program that doesn't compile fails without flashing one
  std::string error{};
  const auto program = kura::Program::Load(options.file, kura::Program::Options{.optimize = options.optimize}, &error);
  if (!program) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return EXIT_FAILURE;
  }

  auto session = kura::Session::Create(program.get(), &error);  // runs `init`
  session->AddListener([](const kura::Session::Event& event) {
    std::println("{} => {}", kura::rt::Format(event.message), kura::rt::Format(event.state));
  });

  // host => UI
  session->Send(kura::rt::MakeVariant("Reset"), &error);

  std::vector<kura::rt::KuraValue> args{};
  if (options.args.empty()) {
    // if (!program->ZeroArguments(options.function, &args, &error)) {
    //   std::fprintf(stderr, "error2: %s\n", error.c_str());
    //   return EXIT_FAILURE;
    // }
  } else {
    for (const auto& arg : options.args)
      args.push_back(kura::ParseArgument(arg));
  }

  glfwSetErrorCallback(&OnGlfwError);
  if (!glfwInit())
    return EXIT_FAILURE;

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
  const auto window = glfwCreateWindow(kFrameSize, kFrameSize, options.file.c_str(), nullptr, nullptr);
  if (window == nullptr) {
    glfwTerminate();
    return EXIT_FAILURE;
  }

  WindowState window_state{};
  glfwSetWindowUserPointer(window, &window_state);
  glfwSetWindowFocusCallback(window, &OnFocus);
  glfwSetScrollCallback(window, &OnScroll);
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  KuraGlOptions gl_options{};
  gl_options.struct_size = sizeof(gl_options);
  gl_options.clear_on_begin = 1;
  gl_options.clear = KuraColor{0.08f, 0.09f, 0.11f, 1.0f};
  KuraRenderer renderer{};
  if (const auto status = kura_gl_renderer_create(&gl_options, &renderer); status != KURA_RENDERER_OK) {
    std::fprintf(stderr, "error: couldn't create the gl renderer (status %d)\n", static_cast<int>(status));
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_FAILURE;
  }

  if (kura_rt_set_renderer(&renderer) != KURA_RENDERER_OK) {
    std::fprintf(stderr, "error: the runtime wouldn't take the renderer\n");
    renderer.ops->destroy(renderer.self);
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_FAILURE;
  }

  // The UI is drawn into the whole framebuffer, so the viewport is the framebuffer's size in pixels (which on a
  // high-DPI display is more than the window's), and it follows the window. Whenever it changes the host tells
  // the UI with the built-in `Resized` message, so the view can use the space it has. The first frame counts as
  // a change, so a UI is told its size before it draws.
  int viewport_width = 0;
  int viewport_height = 0;

  int exit_code = EXIT_SUCCESS;
  long frame = 0;
  std::vector<uint64_t> render_samples{};
  render_samples.reserve(1024);
  while (!glfwWindowShouldClose(window) && (options.frames < 0 || frame < options.frames)) {
    glfwPollEvents();

    {
      int fb_width = 0;
      int fb_height = 0;
      glfwGetFramebufferSize(window, &fb_width, &fb_height);
      // a minimized window has no size to draw into, and nothing to tell the UI
      if (fb_width > 0 && fb_height > 0 && (fb_width != viewport_width || fb_height != viewport_height)) {
        viewport_width = fb_width;
        viewport_height = fb_height;
        kura_rt_set_viewport(static_cast<uint32_t>(fb_width), static_cast<uint32_t>(fb_height));
        // host => UI
        if (!session->Resized(fb_width, fb_height, &error)) {
          std::cerr << "failed to send Resized: " << error << std::endl;
          std::exit(1);
        }
      }

      if (frame == 0 || window_state.focus_changed) {
        window_state.focus_changed = false;
        const auto focused = frame == 0 ? glfwGetWindowAttrib(window, GLFW_FOCUSED) != 0 : window_state.focused;
        if (!session->Focused(focused, &error)) {
          std::cerr << "failed to send Focused: " << error << std::endl;
          std::exit(1);
        }
      }
    }

    {
      // Check if the Escape key is pressed
      if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
      }

      // Check continuous movement keys
      if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        // Move forward
      }

      static bool wasLeftDown = false;
      const bool isLeftDown = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);

      double mx = 0.0;
      double my = 0.0;
      glfwGetCursorPos(window, &mx, &my);

      int winW = 0, winH = 0;
      int fbW = 0, fbH = 0;
      glfwGetWindowSize(window, &winW, &winH);
      glfwGetFramebufferSize(window, &fbW, &fbH);

      if (winW > 0 && winH > 0) {
        const double scaleX = static_cast<double>(fbW) / winW;
        const double scaleY = static_cast<double>(fbH) / winH;
        const double canvasTop = winH - (kura_rt_viewport_height() / scaleY);

        if (isLeftDown && !window_state.pointer_down) {
          // Touch / button press started
          window_state.pointer_down = true;
          window_state.is_dragging = false;
          window_state.press_x = mx;
          window_state.press_y = my;
          window_state.last_x = mx;
          window_state.last_y = my;
        } else if (isLeftDown && window_state.pointer_down) {
          // Finger / cursor moving while held down
          const double totalDx = mx - window_state.press_x;
          const double totalDy = my - window_state.press_y;

          if (!window_state.is_dragging &&
              (totalDx * totalDx + totalDy * totalDy) >= (kDragThreshold * kDragThreshold)) {
            window_state.is_dragging = true;
          }

          if (window_state.is_dragging) {
            const double dx = mx - window_state.last_x;
            const double dy = my - window_state.last_y;
            if (dx != 0.0 || dy != 0.0) {
              bool handled = false;
              // Match `session->Scroll` sign convention: dragging finger up (dy < 0)
              // passes a positive scroll delta to move content down.
              const double uiX = window_state.press_x * scaleX;
              const double uiY = (window_state.press_y - canvasTop) * scaleY;
              if (!session->Scroll(uiX, uiY, -dx * scaleX, -dy * scaleY, &handled, &error)) {
                std::cerr << "failed to scroll: " << error << std::endl;
                std::exit(1);
              }
            }
          }

          window_state.last_x = mx;
          window_state.last_y = my;
        } else if (!isLeftDown && window_state.pointer_down) {
          // Released: only trigger a Click if the gesture never became a scroll drag
          if (!window_state.is_dragging) {
            bool handled = false;
            const double uiX = mx * scaleX;
            const double uiY = (my - canvasTop) * scaleY;
            if (!session->Click(uiX, uiY, &handled, &error)) {
              std::cerr << "failed to invoke click: " << error << std::endl;
              std::exit(1);
            }
          }
          window_state.pointer_down = false;
          window_state.is_dragging = false;
        }
      }
      wasLeftDown = isLeftDown;

      // UI => host: the wheel over a `#scroll` or `#list` scrolls it, and the frame below shows it. GLFW's wheel
      // is positive up and to the left, which is the way the content moves; the UI's is down and to the right.
      if (window_state.wheel_x != 0.0 || window_state.wheel_y != 0.0) {
        double mx = 0.0;
        double my = 0.0;
        glfwGetCursorPos(window, &mx, &my);

        int winW = 0, winH = 0;
        int fbW = 0, fbH = 0;
        glfwGetWindowSize(window, &winW, &winH);
        glfwGetFramebufferSize(window, &fbW, &fbH);
        if (winW > 0 && winH > 0) {
          const double scaleX = static_cast<double>(fbW) / winW;
          const double scaleY = static_cast<double>(fbH) / winH;
          const double canvasTop = winH - (kura_rt_viewport_height() / scaleY);
          bool handled = false;
          if (!session->Scroll(mx * scaleX, (my - canvasTop) * scaleY, -window_state.wheel_x * kPixelsPerNotch,
                               -window_state.wheel_y * kPixelsPerNotch, &handled, &error)) {
            std::cerr << "failed to scroll: " << error << std::endl;
            std::exit(1);
          }
        }
        window_state.wheel_x = 0.0;
        window_state.wheel_y = 0.0;
      }
    }

    PROFILE_BEGIN(render);
    if (!session->Render(&error)) {
      std::cerr << "failed to render: " << error << std::endl;
      std::exit(1);
    }
    PROFILE_END(render);

    glfwSwapBuffers(window);
    frame++;
  }

  const auto render_sample_sum = std::accumulate(render_samples.begin(), render_samples.end(), 0.0);
  const auto render_sample_avg = render_sample_sum / render_samples.size();

  std::println("render avg {:.2f}us over {} frames", render_sample_avg, render_samples.size());

  kura_rt_set_renderer(nullptr);
  renderer.ops->destroy(renderer.self);
  glfwDestroyWindow(window);
  glfwTerminate();
  return exit_code;
}
