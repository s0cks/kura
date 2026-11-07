// A Kura program, from source to pixels: examples/hello-world.kui is parsed, compiled and loaded into
// the JIT, its view is called, and what the compiled code asked the runtime to draw is drawn by the
// OpenGL renderer into an offscreen framebuffer.
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "backend/program.h"
#include "backend/runtime_abi.h"
#include "gl_test_util.h"
#include "renderer/gl/renderer.h"

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

TEST(GlProgramLoad, ASourceThatDoesNotExistIsAnError) {
  std::string error{};
  EXPECT_EQ(kura::Program::Load("/no/such/file.kui", {}, &error), nullptr);
  EXPECT_FALSE(error.empty());
}
}  // namespace
