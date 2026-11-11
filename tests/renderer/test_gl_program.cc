// A Kura program, from source to pixels: examples/hello-world.kui is parsed, compiled and loaded into
// the JIT, its view is called, and what the compiled code asked the runtime to draw is drawn by the
// OpenGL renderer into an offscreen framebuffer.
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <string>
#include <vector>

#include "backend/program.h"
#include "backend/runtime_abi.h"
#include "gl_test_util.h"
#include "renderer/gl/renderer.h"
#include "text/text.h"

#ifndef KURA_EXAMPLES_DIR
#error "KURA_EXAMPLES_DIR must name the examples directory"
#endif  // KURA_EXAMPLES_DIR

namespace {
constexpr uint32_t kViewSize = 1024;

class GlProgramTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!Context().ok())
      GTEST_SKIP() << "no EGL/OpenGL 3.3 context available";
    ASSERT_EQ(kura_gl_renderer_create(nullptr, &renderer_), KURA_RENDERER_OK);
    ASSERT_EQ(kura_rt_set_renderer(&renderer_), KURA_RENDERER_OK);
  }

  void TearDown() override {
    kura_rt_set_renderer(nullptr);
    if (renderer_.ops != nullptr)
      renderer_.ops->destroy(renderer_.self);
  }

  KuraRenderer renderer_{};
};

TEST_F(GlProgramTest, HelloWorldDrawsItsButton) {
  std::string error{};
  const auto program = kura::Program::Load(std::string(KURA_EXAMPLES_DIR) + "/hello-world.kui", {}, &error);
  ASSERT_NE(program, nullptr) << error;

  // `view(count: Int)`: the zero of its parameters is a count of 0
  std::vector<kura::rt::KuraValue> args{};
  ASSERT_TRUE(program->ZeroArguments("view", &args, &error)) << error;
  kura::rt::KuraValue result{};
  ASSERT_TRUE(program->Call("view", args, &result, &error)) << error;

  // `#button(fill: red)`: a red rect, in the middle of the 1024x1024 frame the view begins
  const Frame frame(kViewSize);
  EXPECT_EQ(frame.At(392, 472), kRed);
  EXPECT_EQ(frame.At(631, 551), kRed);
  EXPECT_EQ(frame.At(391, 472), kBlack);
  EXPECT_EQ(frame.At(632, 551), kBlack);
  EXPECT_EQ(frame.At(392, 471), kBlack);
  EXPECT_EQ(frame.At(392, 552), kBlack);
  EXPECT_EQ(frame.Count(kRed), 240u * 80u);
}

TEST_F(GlProgramTest, ACallWithTheWrongArgumentsIsAnErrorNotACrash) {
  std::string error{};
  const auto program = kura::Program::Load(std::string(KURA_EXAMPLES_DIR) + "/hello-world.kui", {}, &error);
  ASSERT_NE(program, nullptr) << error;

  kura::rt::KuraValue result{};
  EXPECT_FALSE(program->Call("view", {}, &result, &error));
  EXPECT_NE(error.find("takes 1 argument"), std::string::npos) << error;
  EXPECT_FALSE(program->Call("no_such_function", {}, &result, &error));
}

// A program's text goes through the whole pipeline: the compiler passes its font family to layout, layout
// measures it with the renderer's font, and the renderer draws it, in the box layout gave it.
TEST_F(GlProgramTest, TextInAFontFamilyIsDrawnInsideItsBlock) {
  kura::text::FontLibrary library{};
  if (library.Find("") == nullptr)
    GTEST_SKIP() << "no system font";

  const auto path = std::filesystem::temp_directory_path() / "kura-gl-text-program.kui";
  {
    std::ofstream out(path);
    out << "fn view(n: Int): Node =>\n"
           "  #block(width: 300px, height: 100px, padding: 10px, fill: blue, align: \"start\") {\n"
           "    #text(value: \"Hello text\", size: 24px, font-family: \"No Such Family 4f1c, sans-serif\", color: "
           "white)\n"
           "  }\n";
  }
  std::string error{};
  const auto program = kura::Program::Load(path.string(), {}, &error);
  ASSERT_NE(program, nullptr) << error;
  std::vector<kura::rt::KuraValue> args{};
  ASSERT_TRUE(program->ZeroArguments("view", &args, &error)) << error;
  kura::rt::KuraValue result{};
  ASSERT_TRUE(program->Call("view", args, &result, &error)) << error;

  const Frame frame(kViewSize);
  // the bounds of the block, and every white pixel, which is text and can only be inside it
  uint32_t min_x = kViewSize, min_y = kViewSize, max_x = 0, max_y = 0;
  size_t blue = 0;
  size_t white = 0;
  size_t text_outside = 0;
  for (uint32_t y = 0; y < kViewSize; y++) {
    for (uint32_t x = 0; x < kViewSize; x++) {
      const auto pixel = frame.At(x, y);
      if (pixel == kBlue) {
        blue++;
        min_x = std::min(min_x, x);
        min_y = std::min(min_y, y);
        max_x = std::max(max_x, x);
        max_y = std::max(max_y, y);
      }
    }
  }
  ASSERT_GT(blue, 0u);
  // the block is 300x100 and its text is white over it, with antialiased pixels in between
  EXPECT_NEAR(static_cast<double>(max_x - min_x + 1), 300.0, 2.0);
  EXPECT_NEAR(static_cast<double>(max_y - min_y + 1), 100.0, 2.0);
  for (uint32_t y = 0; y < kViewSize; y++) {
    for (uint32_t x = 0; x < kViewSize; x++) {
      const auto pixel = frame.At(x, y);
      if (pixel.r > 40 || pixel.g > 40) {  // not black and not the block's blue: ink
        white++;
        if (x < min_x || x > max_x || y < min_y || y > max_y)
          text_outside++;
      }
    }
  }
  EXPECT_GT(white, 30u);
  EXPECT_EQ(text_outside, 0u);
  std::filesystem::remove(path);
}

TEST(GlProgramLoad, ASourceThatDoesNotExistIsAnError) {
  std::string error{};
  EXPECT_EQ(kura::Program::Load("/no/such/file.kui", {}, &error), nullptr);
  EXPECT_FALSE(error.empty());
}
}  // namespace
