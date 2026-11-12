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
  std::cout << #Name << " finished in " << Name##_dur.count() << "us" << std::endl;

#else

#define PROFILE_BEGIN(Name)
#define PROFILE_END(Name)

#endif  // KURA_DEBUG

namespace {
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
  session->AddListener([&program](const kura::Session::Event& event) {
    std::println("{} => {}", kura::rt::Format(event.message), kura::rt::Format(event.state));
  });

  // host => UI
  session->Send(kura::rt::MakeVariant("Reset"), &error);
  const kura::rt::KuraValue five{kura::rt::kTagInt, 5};
  session->Send(kura::rt::MakeVariant("Set", &five), &error);

  std::vector<kura::rt::KuraValue> args{};
  if (options.args.empty()) {
    const auto record = kura_rt_record_new();
    kura_rt_record_set(record, rt::HashName("value"), "value", kura::rt::kTagInt, 0);
    args.push_back({kura::rt::kTagRecord, (uint64_t)record});

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
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
  const auto window = glfwCreateWindow(kFrameSize, kFrameSize, options.file.c_str(), nullptr, nullptr);
  if (window == nullptr) {
    glfwTerminate();
    return EXIT_FAILURE;
  }

  // On a high-DPI display the framebuffer is bigger than the window, and the renderer would draw the
  // 1024x1024 frame into one corner of it. The frame is a fixed size, so shrink the window to match.
  int fb_width = 0;
  int fb_height = 0;
  glfwGetFramebufferSize(window, &fb_width, &fb_height);
  if (fb_width > 0 && fb_height > 0 && (fb_width != kFrameSize || fb_height != kFrameSize))
    glfwSetWindowSize(window, kFrameSize * kFrameSize / fb_width, kFrameSize * kFrameSize / fb_height);
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

  kura_rt_set_viewport(kFrameSize, kFrameSize);

  int exit_code = EXIT_SUCCESS;
  long frame = 0;
  while (!glfwWindowShouldClose(window) && (options.frames < 0 || frame < options.frames)) {
    glfwPollEvents();

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
      bool isLeftDown = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);

      if (isLeftDown && !wasLeftDown) {
        bool handled = false;

        double mx = 0.0;
        double my = 0.0;
        glfwGetCursorPos(window, &mx, &my);

        int winW = 0, winH = 0;
        int fbW = 0, fbH = 0;
        glfwGetWindowSize(window, &winW, &winH);
        glfwGetFramebufferSize(window, &fbW, &fbH);

        if (winW > 0 && winH > 0) {
          double scaleX = static_cast<double>(fbW) / winW;
          double scaleY = static_cast<double>(fbH) / winH;

          // The 1024x1024 physical viewport sits at the bottom-left of the framebuffer
          double canvasTop = winH - (kura_rt_viewport_height() / scaleY);
          double uiX = mx * scaleX;
          double uiY = (my - canvasTop) * scaleY;

          std::cout << "local-ui-pos: " << uiX << ", " << uiY << std::endl;
          session->Click(uiX, uiY, &handled, &error);
          if (!session->Click(uiX, uiY, &handled, &error)) {
            std::cerr << "failed to invoke click: " << error << std::endl;
            std::exit(1);
          }
        }
      }

      wasLeftDown = isLeftDown;
    }

    PROFILE_BEGIN(call);
    if (!session->Render(&error)) {
      std::cerr << "failed to render: " << error << std::endl;
      std::exit(1);
    }
    PROFILE_END(call);

    glfwSwapBuffers(window);
    frame++;
  }

  kura_rt_set_renderer(nullptr);
  renderer.ops->destroy(renderer.self);
  glfwDestroyWindow(window);
  glfwTerminate();
  return exit_code;
}
