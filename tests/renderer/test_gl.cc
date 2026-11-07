// The OpenGL renderer: the ABI conformance suite, and then what only a GPU can show, which is whether
// the right pixels are drawn. The tests render into an offscreen framebuffer of a surfaceless EGL
// context (Mesa's software rasterizer is enough), so no window or display is needed. If no context
// can be made the tests are skipped and the suite isn't registered.
#include <cstdint>
#include <cstdlib>
#include <gtest/gtest.h>
#include <vector>

#include "conformance.h"
#include "gl_test_util.h"
#include "renderer/gl/renderer.h"

namespace {
class GlRendererTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!Context().ok())
      GTEST_SKIP() << "no EGL/OpenGL 3.3 context available";
    ASSERT_EQ(kura_gl_renderer_create(nullptr, &renderer_), KURA_RENDERER_OK);
  }

  void TearDown() override {
    if (renderer_.ops != nullptr)
      renderer_.ops->destroy(renderer_.self);
  }

  auto Ops() -> const KuraRendererOps& {
    return *renderer_.ops;
  }

  auto Begin() -> KuraRendererStatus {
    return Ops().begin_frame(renderer_.self, kSize, kSize);
  }

  auto End() -> KuraRendererStatus {
    return Ops().end_frame(renderer_.self);
  }

  auto Fill(const float x, const float y, const float w, const float h, const KuraColor& color) -> KuraRendererStatus {
    return Ops().fill_rect(renderer_.self, KuraRect{x, y, w, h}, color);
  }

  KuraRenderer renderer_{};
};

constexpr KuraColor kRedColor = {1.0f, 0.0f, 0.0f, 1.0f};
constexpr KuraColor kBlueColor = {0.0f, 0.0f, 1.0f, 1.0f};

TEST_F(GlRendererTest, FillRectDrawsExactlyItsPixels) {
  ASSERT_EQ(Begin(), KURA_RENDERER_OK);
  ASSERT_EQ(Fill(10, 20, 30, 40, kRedColor), KURA_RENDERER_OK);
  ASSERT_EQ(End(), KURA_RENDERER_OK);

  const Frame frame;
  // the origin is the top-left, and the rect is [10, 40) x [20, 60)
  EXPECT_EQ(frame.At(10, 20), kRed);
  EXPECT_EQ(frame.At(39, 59), kRed);
  EXPECT_EQ(frame.At(9, 20), kBlack);
  EXPECT_EQ(frame.At(40, 20), kBlack);
  EXPECT_EQ(frame.At(10, 19), kBlack);
  EXPECT_EQ(frame.At(10, 60), kBlack);
  EXPECT_EQ(frame.Count(kRed), 30u * 40u);
}

TEST_F(GlRendererTest, LaterDrawsCoverEarlierOnes) {
  ASSERT_EQ(Begin(), KURA_RENDERER_OK);
  ASSERT_EQ(Fill(0, 0, 50, 50, kRedColor), KURA_RENDERER_OK);
  ASSERT_EQ(Fill(25, 25, 50, 50, kBlueColor), KURA_RENDERER_OK);
  ASSERT_EQ(End(), KURA_RENDERER_OK);

  const Frame frame;
  EXPECT_EQ(frame.At(10, 10), kRed);
  EXPECT_EQ(frame.At(30, 30), kBlue);
  EXPECT_EQ(frame.At(60, 60), kBlue);
}

TEST_F(GlRendererTest, ColorsBlendSourceOver) {
  ASSERT_EQ(Begin(), KURA_RENDERER_OK);
  ASSERT_EQ(Fill(0, 0, 10, 10, KuraColor{0.0f, 1.0f, 0.0f, 0.5f}), KURA_RENDERER_OK);
  ASSERT_EQ(End(), KURA_RENDERER_OK);

  // half green over the opaque black the frame was cleared to
  EXPECT_TRUE(Near(Frame().At(5, 5), Pixel{0, 128, 0, 255}, 2));
}

TEST_F(GlRendererTest, ClipsRestrictDrawing) {
  ASSERT_EQ(Begin(), KURA_RENDERER_OK);
  ASSERT_EQ(Ops().push_clip(renderer_.self, KuraRect{0, 0, 20, 10}), KURA_RENDERER_OK);
  ASSERT_EQ(Fill(0, 0, 100, 100, kRedColor), KURA_RENDERER_OK);
  ASSERT_EQ(Ops().push_clip(renderer_.self, KuraRect{10, 0, 100, 5}), KURA_RENDERER_OK);
  ASSERT_EQ(Fill(0, 0, 100, 100, kBlueColor), KURA_RENDERER_OK);
  ASSERT_EQ(Ops().pop_clip(renderer_.self), KURA_RENDERER_OK);
  ASSERT_EQ(Ops().pop_clip(renderer_.self), KURA_RENDERER_OK);
  ASSERT_EQ(Fill(100, 100, 10, 10, kRedColor), KURA_RENDERER_OK);
  ASSERT_EQ(End(), KURA_RENDERER_OK);

  const Frame frame;
  // the outer clip is [0, 20) x [0, 10) and the inner one [10, 20) x [0, 5) inside it
  EXPECT_EQ(frame.Count(kBlue), 10u * 5u);
  EXPECT_EQ(frame.At(10, 0), kBlue);
  EXPECT_EQ(frame.At(9, 0), kRed);
  EXPECT_EQ(frame.At(15, 7), kRed);
  EXPECT_EQ(frame.At(25, 5), kBlack);
  // with the clips popped, drawing is unrestricted again
  EXPECT_EQ(frame.At(105, 105), kRed);
  EXPECT_EQ(frame.Count(kRed), 20u * 10u - 10u * 5u + 10u * 10u);
}

TEST_F(GlRendererTest, StrokeRectIsAOnePixelRingInsideTheRect) {
  ASSERT_EQ(Begin(), KURA_RENDERER_OK);
  ASSERT_EQ(Ops().stroke_rect(renderer_.self, KuraRect{10, 10, 10, 8}, kRedColor), KURA_RENDERER_OK);
  ASSERT_EQ(End(), KURA_RENDERER_OK);

  const Frame frame;
  EXPECT_EQ(frame.Count(kRed), 2u * 10u + 2u * (8u - 2u));
  EXPECT_EQ(frame.At(10, 10), kRed);
  EXPECT_EQ(frame.At(19, 17), kRed);
  EXPECT_EQ(frame.At(15, 13), kBlack);
  EXPECT_EQ(frame.At(20, 10), kBlack);
}

TEST_F(GlRendererTest, HorizontalLinesAreOnePixelThick) {
  ASSERT_EQ(Begin(), KURA_RENDERER_OK);
  ASSERT_EQ(Ops().draw_line(renderer_.self, KuraVec2{10.0f, 20.5f}, KuraVec2{30.0f, 20.5f}, kRedColor),
            KURA_RENDERER_OK);
  ASSERT_EQ(End(), KURA_RENDERER_OK);

  const Frame frame;
  EXPECT_EQ(frame.At(15, 20), kRed);
  EXPECT_EQ(frame.At(15, 19), kBlack);
  EXPECT_EQ(frame.At(15, 21), kBlack);
  EXPECT_EQ(frame.Count(kRed), 20u);
}

TEST_F(GlRendererTest, MoreDrawCallsThanFitInABatchAreAllDrawn) {
  // 100 x 100 one-pixel rects: more quads than the index buffer holds, so the batch flushes midway
  ASSERT_EQ(Begin(), KURA_RENDERER_OK);
  for (uint32_t y = 0; y < 100; y++) {
    for (uint32_t x = 0; x < 100; x++)
      ASSERT_EQ(Fill(static_cast<float>(x), static_cast<float>(y), 1, 1, kRedColor), KURA_RENDERER_OK);
  }
  ASSERT_EQ(End(), KURA_RENDERER_OK);
  EXPECT_EQ(Frame().Count(kRed), 100u * 100u);
}

TEST_F(GlRendererTest, AFrameLeavesTheHostsGlStateAsItWasFound) {
  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
  glEnable(GL_DEPTH_TEST);
  glViewport(1, 2, 3, 4);
  glUseProgram(0);
  glBindVertexArray(0);

  ASSERT_EQ(Begin(), KURA_RENDERER_OK);
  ASSERT_EQ(Ops().push_clip(renderer_.self, KuraRect{0, 0, 5, 5}), KURA_RENDERER_OK);
  ASSERT_EQ(Fill(0, 0, 10, 10, kRedColor), KURA_RENDERER_OK);
  ASSERT_EQ(Ops().pop_clip(renderer_.self), KURA_RENDERER_OK);
  ASSERT_EQ(End(), KURA_RENDERER_OK);

  GLint viewport[4];
  glGetIntegerv(GL_VIEWPORT, viewport);
  EXPECT_EQ(viewport[0], 1);
  EXPECT_EQ(viewport[1], 2);
  EXPECT_EQ(viewport[2], 3);
  EXPECT_EQ(viewport[3], 4);
  GLint program = -1;
  glGetIntegerv(GL_CURRENT_PROGRAM, &program);
  EXPECT_EQ(program, 0);
  GLint vao = -1;
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
  EXPECT_EQ(vao, 0);
  EXPECT_FALSE(glIsEnabled(GL_BLEND));
  EXPECT_FALSE(glIsEnabled(GL_SCISSOR_TEST));
  EXPECT_TRUE(glIsEnabled(GL_DEPTH_TEST));
  glDisable(GL_DEPTH_TEST);
  glViewport(0, 0, kSize, kSize);
}

TEST_F(GlRendererTest, ClearingCanBeTurnedOffToDrawOverWhatIsThere) {
  renderer_.ops->destroy(renderer_.self);
  KuraGlOptions options{};
  options.struct_size = sizeof(options);
  options.clear_on_begin = 1;
  options.clear = KuraColor{0.0f, 0.0f, 1.0f, 1.0f};
  ASSERT_EQ(kura_gl_renderer_create(&options, &renderer_), KURA_RENDERER_OK);
  ASSERT_EQ(Begin(), KURA_RENDERER_OK);
  ASSERT_EQ(End(), KURA_RENDERER_OK);
  EXPECT_EQ(Frame().At(0, 0), kBlue);

  renderer_.ops->destroy(renderer_.self);
  options.clear_on_begin = 0;
  options.clear = KuraColor{1.0f, 0.0f, 0.0f, 1.0f};
  ASSERT_EQ(kura_gl_renderer_create(&options, &renderer_), KURA_RENDERER_OK);
  ASSERT_EQ(Begin(), KURA_RENDERER_OK);
  ASSERT_EQ(Fill(0, 0, 4, 4, kRedColor), KURA_RENDERER_OK);
  ASSERT_EQ(End(), KURA_RENDERER_OK);
  const Frame frame;
  EXPECT_EQ(frame.At(2, 2), kRed);
  EXPECT_EQ(frame.At(50, 50), kBlue);  // the blue is still there from before
}

TEST(GlRendererCreate, RejectsBadArguments) {
  if (!Context().ok())
    GTEST_SKIP() << "no EGL/OpenGL 3.3 context available";
  EXPECT_EQ(kura_gl_renderer_create(nullptr, nullptr), KURA_RENDERER_ERR_INVALID_ARG);
  KuraRenderer renderer{};
  KuraGlOptions options{};
  options.struct_size = 1;
  EXPECT_EQ(kura_gl_renderer_create(&options, &renderer), KURA_RENDERER_ERR_INVALID_ARG);
}

struct Registration {
  Registration() {
    if (!Context().ok())
      return;
    kura::test::RegisterConformanceSuite("OpenGL", [] {
      KuraRenderer renderer{};
      if (kura_gl_renderer_create(nullptr, &renderer) != KURA_RENDERER_OK)
        return KuraRenderer{};
      return renderer;
    });
  }
};

const Registration registration{};
}  // namespace
