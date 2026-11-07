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
#include <string>
#include <vector>

#include "backend/program.h"
#include "backend/runtime_abi.h"
#include "renderer/gl/renderer.h"

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
  Options options{};
  if (!ParseOptions(argc, argv, &options)) {
    std::fprintf(stderr, "usage: kura-gl-demo <file.kui> [args...] [--function <name>] [--frames <n>] [--no-opt]\n");
    return EXIT_FAILURE;
  }

  // compile before opening a window, so a program that doesn't compile fails without flashing one
  std::string error{};
  const auto program = kura::Program::Load(options.file, kura::Program::Options{.optimize = options.optimize}, &error);
  if (!program) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return EXIT_FAILURE;
  }

  std::vector<kura::rt::KuraValue> args{};
  if (options.args.empty()) {
    if (!program->ZeroArguments(options.function, &args, &error)) {
      std::fprintf(stderr, "error2: %s\n", error.c_str());
      return EXIT_FAILURE;
    }
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

  int exit_code = EXIT_SUCCESS;
  long frame = 0;
  while (!glfwWindowShouldClose(window) && (options.frames < 0 || frame < options.frames)) {
    glfwPollEvents();

    const auto call_start = std::chrono::high_resolution_clock::now();
    kura::rt::KuraValue result{};
    if (!program->Call(options.function, args, &result, &error)) {
      std::fprintf(stderr, "error3: %s\n", error.c_str());
      exit_code = EXIT_FAILURE;
      break;
    }
    const auto call_end = std::chrono::high_resolution_clock::now();
    const auto call_dur = std::chrono::duration_cast<std::chrono::microseconds>((call_end - call_start));
    std::cout << "call finished in " << call_dur.count() << "us" << std::endl;

    glfwSwapBuffers(window);
    frame++;
  }

  kura_rt_set_renderer(nullptr);
  renderer.ops->destroy(renderer.self);
  glfwDestroyWindow(window);
  glfwTerminate();
  return exit_code;
}
