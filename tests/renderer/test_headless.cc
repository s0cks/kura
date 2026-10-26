#include <cmath>
#include <limits>
#include <string>

#include "headless_util.h"

using namespace kura::test;
using namespace kura::renderer;

namespace {
auto Options(const bool rasterize, const KuraColor& clear = {0, 0, 0, 0}) -> HeadlessOptions {
  HeadlessOptions options{};
  options.rasterize = rasterize;
  options.clear = clear;
  return options;
}
}  // namespace

// ---- commands -------------------------------------------------------------

TEST(Headless, RecordsEveryCallInOrder) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  draw.Begin(32, 16);
  draw.Fill({1, 2, 3, 4}, kRed);
  draw.Stroke({5, 6, 7, 8}, kGreen);
  draw.Line({1, 1}, {9, 9}, kBlue);
  draw.Text({2, 3}, "hi", 12, kWhite);
  draw.Push({0, 0, 10, 10});
  draw.Pop();
  draw.End();

  const auto& commands = renderer.GetCommands();
  ASSERT_EQ(commands.size(), 6u);

  EXPECT_TRUE(commands[0].kind == Command::Kind::kFillRect);
  EXPECT_EQ(commands[0].rect.x, 1.0f);
  EXPECT_EQ(commands[0].rect.height, 4.0f);
  EXPECT_EQ(commands[0].color.r, 1.0f);

  EXPECT_TRUE(commands[1].kind == Command::Kind::kStrokeRect);
  EXPECT_EQ(commands[1].rect.y, 6.0f);
  EXPECT_EQ(commands[1].color.g, 1.0f);

  EXPECT_TRUE(commands[2].kind == Command::Kind::kLine);
  EXPECT_EQ(commands[2].from.x, 1.0f);
  EXPECT_EQ(commands[2].to.y, 9.0f);

  EXPECT_TRUE(commands[3].kind == Command::Kind::kText);
  EXPECT_EQ(commands[3].text, std::string("hi"));
  EXPECT_EQ(commands[3].font_size, 12.0f);
  EXPECT_EQ(commands[3].from.y, 3.0f);

  EXPECT_TRUE(commands[4].kind == Command::Kind::kPushClip);
  EXPECT_EQ(commands[4].rect.width, 10.0f);
  EXPECT_TRUE(commands[5].kind == Command::Kind::kPopClip);
}

TEST(Headless, RejectedCallsAreNotRecorded) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  draw.Begin(8, 8);
  EXPECT_EQ(draw.Ops().fill_rect(draw.Self(), {nan, 0, 1, 1}, kRed), KURA_RENDERER_ERR_INVALID_ARG);
  EXPECT_EQ(draw.Ops().pop_clip(draw.Self()), KURA_RENDERER_ERR_CLIP_UNDERFLOW);
  EXPECT_EQ(draw.Ops().draw_text(draw.Self(), {0, 0}, nullptr, 2, KuraTextStyle{8}, kRed),
                 KURA_RENDERER_ERR_INVALID_ARG);
  draw.End();
  EXPECT_TRUE(renderer.GetCommands().empty());
}

TEST(Headless, CommandsOutliveTheFrameAndTheNextFrameClearsThem) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  draw.Begin(8, 8);
  draw.Fill({0, 0, 4, 4}, kRed);
  draw.End();
  EXPECT_EQ(renderer.GetCommands().size(), 1u);
  EXPECT_EQ(renderer.PixelAt(0, 0), kRedPixel);

  draw.Begin(8, 8);
  EXPECT_TRUE(renderer.GetCommands().empty());
  EXPECT_EQ(renderer.PixelAt(0, 0), kClearPixel);
  draw.End();
}

TEST(Headless, CommandsCompareByValue) {
  Command a{};
  Command b{};
  EXPECT_TRUE(a == b);
  b.text = "x";
  EXPECT_TRUE(a != b);
  b = a;
  b.rect.width = 1.0f;
  EXPECT_TRUE(a != b);
}

// ---- frame state ----------------------------------------------------------

TEST(Headless, TracksTheFrame) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  EXPECT_TRUE(!renderer.InFrame());
  EXPECT_EQ(renderer.GetFrameCount(), 0u);

  draw.Begin(20, 10);
  EXPECT_TRUE(renderer.InFrame());
  EXPECT_EQ(renderer.GetWidth(), 20u);
  EXPECT_EQ(renderer.GetHeight(), 10u);
  draw.Push({0, 0, 5, 5});
  draw.Push({0, 0, 5, 5});
  EXPECT_EQ(renderer.GetClipDepth(), 2u);
  draw.Pop();
  EXPECT_EQ(renderer.GetClipDepth(), 1u);
  draw.Pop();
  draw.End();
  EXPECT_TRUE(!renderer.InFrame());
  EXPECT_EQ(renderer.GetFrameCount(), 1u);
}

TEST(Headless, AFrameEndedWithClipsStillCounts) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  draw.Begin(8, 8);
  draw.Push({0, 0, 4, 4});
  EXPECT_EQ(draw.Ops().end_frame(draw.Self()), KURA_RENDERER_ERR_CLIP_UNBALANCED);
  EXPECT_EQ(renderer.GetFrameCount(), 1u);
  EXPECT_EQ(renderer.GetClipDepth(), 0u);
  EXPECT_TRUE(!renderer.InFrame());
}

TEST(Headless, TheSurfaceCanChangeSizeBetweenFrames) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  draw.Begin(8, 8);
  draw.End();
  EXPECT_EQ(renderer.GetPixels().size(), 8u * 8u * 4u);
  draw.Begin(4, 2);
  draw.Fill({0, 0, 100, 100}, kRed);
  draw.End();
  EXPECT_EQ(renderer.GetWidth(), 4u);
  EXPECT_EQ(renderer.GetHeight(), 2u);
  EXPECT_EQ(renderer.GetPixels().size(), 4u * 2u * 4u);
  EXPECT_EQ(renderer.CountPixels(kRedPixel), 8u);
}

TEST(Headless, EveryFrameStartsCleared) {
  HeadlessRenderer renderer(Options(true, kBlue));
  Drawer draw(renderer);
  draw.Begin(4, 4);
  EXPECT_EQ(renderer.CountPixels(kBluePixel), 16u);
  draw.Fill({0, 0, 2, 2}, kRed);
  draw.End();
  EXPECT_EQ(renderer.CountPixels(kRedPixel), 4u);
  draw.Begin(4, 4);
  EXPECT_EQ(renderer.CountPixels(kBluePixel), 16u);
  draw.End();
}

// ---- fills ----------------------------------------------------------------

TEST(Headless, FillCoversThePixelsWhoseCentersAreInside) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  draw.Begin(16, 16);
  draw.Fill({0, 0, 10, 10}, kRed);
  draw.End();
  EXPECT_EQ(renderer.CountPixels(kRedPixel), 100u);
  EXPECT_EQ(renderer.PixelAt(0, 0), kRedPixel);
  EXPECT_EQ(renderer.PixelAt(9, 9), kRedPixel);
  EXPECT_EQ(renderer.PixelAt(10, 9), kClearPixel);
  EXPECT_EQ(renderer.PixelAt(9, 10), kClearPixel);
}

TEST(Headless, FillIncludesTheLeftAndTopEdgesButNotTheRightAndBottom) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  draw.Begin(16, 16);
  // pixel 0's center (0.5) is exactly on this rect's left and top edges, and
  // pixel 10's center (10.5) is exactly on its right and bottom ones
  draw.Fill({0.5f, 0.5f, 10, 10}, kRed);
  draw.End();
  EXPECT_EQ(renderer.CountPixels(kRedPixel), 100u);
  EXPECT_EQ(renderer.PixelAt(0, 0), kRedPixel);
  EXPECT_EQ(renderer.PixelAt(9, 9), kRedPixel);
  EXPECT_EQ(renderer.PixelAt(10, 10), kClearPixel);
}

TEST(Headless, FillPastAHalfPixelMovesTheCoverage) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  draw.Begin(16, 16);
  draw.Fill({0.6f, 0.6f, 10, 10}, kRed);
  draw.End();
  EXPECT_EQ(renderer.CountPixels(kRedPixel), 100u);
  EXPECT_EQ(renderer.PixelAt(0, 0), kClearPixel);
  EXPECT_EQ(renderer.PixelAt(1, 1), kRedPixel);
  EXPECT_EQ(renderer.PixelAt(10, 10), kRedPixel);
  EXPECT_EQ(renderer.PixelAt(11, 11), kClearPixel);
}

TEST(Headless, FillThatHoldsNoPixelCenterDrawsNothing) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  draw.Begin(8, 8);
  draw.Fill({0.6f, 0.6f, 0.2f, 0.2f}, kRed);
  draw.Fill({3, 3, 0, 5}, kRed);
  draw.End();
  EXPECT_EQ(renderer.CountPixels(kRedPixel), 0u);
  // ...but a sliver that does hold one draws it
  draw.Begin(8, 8);
  draw.Fill({0.4f, 0.4f, 0.2f, 0.2f}, kRed);
  draw.End();
  EXPECT_EQ(renderer.CountPixels(kRedPixel), 1u);
  EXPECT_EQ(renderer.PixelAt(0, 0), kRedPixel);
}

TEST(Headless, FillIsClippedToTheSurface) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  draw.Begin(8, 8);
  draw.Fill({-5, -5, 10, 10}, kRed);
  draw.Fill({6, 6, 10, 10}, kGreen);
  draw.End();
  EXPECT_EQ(renderer.CountPixels(kRedPixel), 25u);
  EXPECT_EQ(renderer.CountPixels(kGreenPixel), 4u);
}

TEST(Headless, LaterDrawsAreOnTop) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  draw.Begin(8, 8);
  draw.Fill({0, 0, 8, 8}, kRed);
  draw.Fill({2, 2, 4, 4}, kBlue);
  draw.End();
  EXPECT_EQ(renderer.PixelAt(1, 1), kRedPixel);
  EXPECT_EQ(renderer.PixelAt(2, 2), kBluePixel);
  EXPECT_EQ(renderer.PixelAt(5, 5), kBluePixel);
  EXPECT_EQ(renderer.PixelAt(6, 6), kRedPixel);
}

TEST(Headless, HugeCoordinatesAreSafe) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  draw.Begin(8, 8);
  draw.Fill({-1e30f, -1e30f, 2e30f, 2e30f}, kRed);
  draw.Fill({1e30f, 1e30f, 1e30f, 1e30f}, kBlue);
  draw.Line({-1e30f, -1e30f}, {1e30f, 1e30f}, kGreen);
  draw.Stroke({-1e30f, 0, 1e30f, 4}, kYellow);
  draw.End();
  EXPECT_EQ(renderer.CountPixels(kBluePixel), 0u);
  // the line is the diagonal, on top of the red
  EXPECT_EQ(renderer.PixelAt(3, 3), kGreenPixel);
}

// ---- colors ---------------------------------------------------------------

TEST(Headless, ChannelsAreClampedAndRounded) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  draw.Begin(2, 1);
  draw.Fill({0, 0, 1, 1}, {2.0f, -1.0f, 0.5f, 1.0f});
  draw.End();
  EXPECT_EQ(renderer.PixelAt(0, 0), (Rgba8{255, 0, 128, 255}));
}

TEST(Headless, OpaqueOverwritesAndTransparentDoesNothing) {
  HeadlessRenderer renderer(Options(true, kBlue));
  Drawer draw(renderer);
  draw.Begin(2, 1);
  draw.Fill({0, 0, 1, 1}, kRed);
  draw.Fill({1, 0, 1, 1}, {1.0f, 0.0f, 0.0f, 0.0f});
  draw.End();
  EXPECT_EQ(renderer.PixelAt(0, 0), kRedPixel);
  EXPECT_EQ(renderer.PixelAt(1, 0), kBluePixel);
}

TEST(Headless, BlendsOverAnOpaquePixel) {
  HeadlessRenderer renderer(Options(true, kBlue));
  Drawer draw(renderer);
  draw.Begin(1, 1);
  draw.Fill({0, 0, 1, 1}, {1.0f, 0.0f, 0.0f, 0.5f});
  draw.End();
  EXPECT_EQ(renderer.PixelAt(0, 0), (Rgba8{128, 0, 127, 255}));
}

TEST(Headless, BlendsOverATransparentPixelWithoutPremultiplying) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  draw.Begin(1, 1);
  draw.Fill({0, 0, 1, 1}, {1.0f, 0.0f, 0.0f, 0.5f});
  draw.End();
  EXPECT_EQ(renderer.PixelAt(0, 0), (Rgba8{255, 0, 0, 128}));
}

TEST(Headless, BlendingRoundsToTheNearestValue) {
  // 128 at 77/255 over white is 255 - 77 * (255 - 128) / 255 = 216.65, which is 217
  HeadlessRenderer renderer(Options(true, kWhite));
  Drawer draw(renderer);
  draw.Begin(1, 1);
  draw.Fill({0, 0, 1, 1}, {128.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f, 77.0f / 255.0f});
  draw.End();
  EXPECT_EQ(renderer.PixelAt(0, 0), (Rgba8{217, 217, 217, 255}));
}

TEST(Headless, BlendingTwiceKeepsAccumulating) {
  HeadlessRenderer renderer(Options(true, kWhite));
  Drawer draw(renderer);
  draw.Begin(1, 1);
  draw.Fill({0, 0, 1, 1}, {0.0f, 0.0f, 0.0f, 0.5f});
  const auto once = renderer.PixelAt(0, 0);
  draw.Fill({0, 0, 1, 1}, {0.0f, 0.0f, 0.0f, 0.5f});
  const auto twice = renderer.PixelAt(0, 0);
  draw.End();
  EXPECT_TRUE(once.r > twice.r);
  EXPECT_TRUE(twice.r > 0);
  EXPECT_EQ(twice.a, 255);
}

// ---- strokes and lines ----------------------------------------------------

TEST(Headless, StrokeIsTheOutsideRingOfTheFill) {
  HeadlessRenderer renderer(Options(true, kWhite));
  Drawer draw(renderer);
  draw.Begin(10, 10);
  draw.Stroke({1, 1, 6, 6}, kRed);
  draw.End();
  EXPECT_EQ(renderer.CountPixels(kRedPixel), 20u);
  EXPECT_EQ(renderer.PixelAt(1, 1), kRedPixel);
  EXPECT_EQ(renderer.PixelAt(6, 6), kRedPixel);
  EXPECT_EQ(renderer.PixelAt(3, 1), kRedPixel);
  EXPECT_EQ(renderer.PixelAt(1, 4), kRedPixel);
  EXPECT_EQ(renderer.PixelAt(3, 3), kWhitePixel);
  EXPECT_EQ(renderer.PixelAt(0, 0), kWhitePixel);
  EXPECT_EQ(renderer.PixelAt(7, 7), kWhitePixel);
}

TEST(Headless, StrokeBlendsEveryPixelOnce) {
  HeadlessRenderer renderer(Options(true, kWhite));
  Drawer draw(renderer);
  draw.Begin(10, 10);
  draw.Stroke({1, 1, 6, 6}, {0.0f, 0.0f, 0.0f, 0.5f});
  draw.End();
  // a corner isn't darker than an edge
  EXPECT_EQ(renderer.PixelAt(1, 1), renderer.PixelAt(3, 1));
  EXPECT_EQ(renderer.PixelAt(6, 6), renderer.PixelAt(6, 3));
  EXPECT_NE(renderer.PixelAt(1, 1), kWhitePixel);
}

TEST(Headless, StrokeOfAThinRectIsTheWholeRect) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  draw.Begin(10, 10);
  draw.Stroke({2, 2, 1, 5}, kRed);
  draw.End();
  EXPECT_EQ(renderer.CountPixels(kRedPixel), 5u);

  draw.Begin(10, 10);
  draw.Stroke({2, 2, 5, 1}, kRed);
  draw.End();
  EXPECT_EQ(renderer.CountPixels(kRedPixel), 5u);

  draw.Begin(10, 10);
  draw.Stroke({2, 2, 2, 2}, kRed);
  draw.End();
  EXPECT_EQ(renderer.CountPixels(kRedPixel), 4u);

  draw.Begin(10, 10);
  draw.Stroke({2, 2, 0, 4}, kRed);
  draw.End();
  EXPECT_EQ(renderer.CountPixels(kRedPixel), 0u);
}

TEST(Headless, LinesIncludeBothEnds) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  draw.Begin(8, 8);
  draw.Line({0, 0}, {4, 0}, kRed);
  draw.Line({0, 2}, {0, 6}, kGreen);
  draw.Line({2, 2}, {5, 5}, kBlue);
  draw.End();
  EXPECT_EQ(renderer.CountPixels(kRedPixel), 5u);
  EXPECT_EQ(renderer.CountPixels(kGreenPixel), 5u);
  EXPECT_EQ(renderer.CountPixels(kBluePixel), 4u);
  for (uint32_t idx = 2; idx <= 5; idx++)
    EXPECT_EQ(renderer.PixelAt(idx, idx), kBluePixel);
}

TEST(Headless, ALineCoversThePixelsContainingItsEnds) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  draw.Begin(8, 8);
  draw.Line({0.9f, 0.1f}, {3.1f, 3.9f}, kRed);
  draw.End();
  EXPECT_EQ(renderer.PixelAt(0, 0), kRedPixel);
  EXPECT_EQ(renderer.PixelAt(3, 3), kRedPixel);
  EXPECT_EQ(renderer.CountPixels(kRedPixel), 4u);
}

TEST(Headless, ALineHasOnePixelPerStepOfItsLongerAxis) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  draw.Begin(16, 16);
  draw.Line({0, 0}, {10, 3}, kRed);
  draw.End();
  EXPECT_EQ(renderer.CountPixels(kRedPixel), 11u);
  for (uint32_t x = 0; x <= 10; x++) {
    auto in_column = 0;
    for (uint32_t y = 0; y < 16; y++)
      in_column += renderer.PixelAt(x, y) == kRedPixel ? 1 : 0;
    EXPECT_EQ(in_column, 1);
  }
  EXPECT_EQ(renderer.PixelAt(0, 0), kRedPixel);
  EXPECT_EQ(renderer.PixelAt(10, 3), kRedPixel);
}

TEST(Headless, ALineDrawnBackwardsIsTheSameLine) {
  HeadlessRenderer forward;
  HeadlessRenderer backward;
  Drawer a(forward);
  Drawer b(backward);
  a.Begin(16, 16);
  a.Line({1, 2}, {12, 9}, kRed);
  a.End();
  b.Begin(16, 16);
  b.Line({12, 9}, {1, 2}, kRed);
  b.End();
  EXPECT_EQ(forward.CountPixels(kRedPixel), backward.CountPixels(kRedPixel));
  EXPECT_TRUE(forward.GetPixels() == backward.GetPixels());
}

TEST(Headless, APointIsOnePixel) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  draw.Begin(4, 4);
  draw.Line({2.5f, 1.5f}, {2.5f, 1.5f}, kRed);
  draw.End();
  EXPECT_EQ(renderer.CountPixels(kRedPixel), 1u);
  EXPECT_EQ(renderer.PixelAt(2, 1), kRedPixel);
}

// ---- clipping -------------------------------------------------------------

TEST(Headless, ClipRestrictsEveryKindOfDrawing) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  draw.Begin(10, 10);
  draw.Push({2, 2, 4, 4});
  draw.Fill({0, 0, 10, 10}, kRed);
  draw.Line({0, 0}, {9, 9}, kGreen);
  draw.Stroke({0, 0, 10, 10}, kBlue);
  draw.Pop();
  draw.End();
  EXPECT_EQ(renderer.CountPixels(kRedPixel) + renderer.CountPixels(kGreenPixel), 16u);
  // nothing outside the clip was touched
  EXPECT_EQ(renderer.PixelAt(1, 1), kClearPixel);
  EXPECT_EQ(renderer.PixelAt(6, 6), kClearPixel);
  EXPECT_EQ(renderer.PixelAt(0, 5), kClearPixel);
  // the diagonal is on top of the fill
  EXPECT_EQ(renderer.PixelAt(3, 3), kGreenPixel);
  EXPECT_EQ(renderer.PixelAt(2, 3), kRedPixel);
  // the stroke's ring is all outside the clip
  EXPECT_EQ(renderer.CountPixels(kBluePixel), 0u);
}

TEST(Headless, ClipsNestByIntersection) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  draw.Begin(12, 12);
  draw.Push({2, 2, 6, 6});
  draw.Push({4, 4, 6, 6});
  EXPECT_EQ(renderer.GetClipDepth(), 2u);
  draw.Fill({0, 0, 12, 12}, kBlue);
  // [4, 8) x [4, 8)
  EXPECT_EQ(renderer.CountPixels(kBluePixel), 16u);
  draw.Pop();
  draw.Fill({0, 0, 3, 3}, kGreen);
  // back to [2, 8): only (2, 2) of that rect is inside
  EXPECT_EQ(renderer.CountPixels(kGreenPixel), 1u);
  EXPECT_EQ(renderer.PixelAt(2, 2), kGreenPixel);
  draw.Pop();
  draw.Fill({0, 0, 1, 1}, kYellow);
  EXPECT_EQ(renderer.PixelAt(0, 0), kYellowPixel);
  draw.End();
}

TEST(Headless, ANestedClipCannotEscapeItsParent) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  draw.Begin(10, 10);
  draw.Push({0, 0, 4, 4});
  draw.Push({0, 0, 10, 10});
  draw.Fill({0, 0, 10, 10}, kRed);
  draw.Pop();
  draw.Pop();
  draw.End();
  EXPECT_EQ(renderer.CountPixels(kRedPixel), 16u);
}

TEST(Headless, AnEmptyClipHidesEverythingUntilItIsPopped) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  draw.Begin(10, 10);
  draw.Push({100, 100, 5, 5});
  draw.Fill({0, 0, 10, 10}, kRed);
  draw.Push({0, 0, 10, 10});
  draw.Fill({0, 0, 10, 10}, kRed);
  draw.Pop();
  draw.Pop();
  EXPECT_EQ(renderer.CountPixels(kRedPixel), 0u);
  draw.Push({0, 0, 0, 0});
  draw.Line({0, 0}, {9, 9}, kRed);
  draw.Pop();
  EXPECT_EQ(renderer.CountPixels(kRedPixel), 0u);
  draw.Fill({0, 0, 2, 2}, kRed);
  EXPECT_EQ(renderer.CountPixels(kRedPixel), 4u);
  draw.End();
}

TEST(Headless, ClipsDontOutliveTheirFrame) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  draw.Begin(8, 8);
  draw.Push({0, 0, 1, 1});
  EXPECT_EQ(draw.Ops().end_frame(draw.Self()), KURA_RENDERER_ERR_CLIP_UNBALANCED);
  draw.Begin(8, 8);
  draw.Fill({0, 0, 8, 8}, kRed);
  draw.End();
  EXPECT_EQ(renderer.CountPixels(kRedPixel), 64u);
}

// ---- text -----------------------------------------------------------------

TEST(Headless, TextIsRecordedButNotRasterized) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  draw.Begin(32, 16);
  draw.Text({2, 2}, "Hello", 12, kRed);
  draw.End();
  EXPECT_EQ(renderer.CountPixels(kClearPixel), 32u * 16u);
  ASSERT_EQ(renderer.GetCommands().size(), 1u);
  EXPECT_EQ(renderer.GetCommands()[0].text, std::string("Hello"));
}

TEST(Headless, TextKeepsEveryByteIncludingNul) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  draw.Begin(8, 8);
  const std::string text("a\0b\xc3\xa9", 5);
  draw.Text({0, 0}, text, 8, kRed);
  draw.End();
  ASSERT_EQ(renderer.GetCommands().size(), 1u);
  EXPECT_EQ(renderer.GetCommands()[0].text.size(), 5u);
  EXPECT_TRUE(renderer.GetCommands()[0].text == text);
}

TEST(Headless, TextMetricsAreTheDocumentedModel) {
  // each code point is size / 2 wide; a line is size * 1.25 tall
  auto metrics = HeadlessRenderer::MeasureText("", 16);
  EXPECT_EQ(metrics.width, 0.0f);
  EXPECT_EQ(metrics.height, 20.0f);
  EXPECT_EQ(metrics.ascent, 16.0f);

  metrics = HeadlessRenderer::MeasureText("abc", 16);
  EXPECT_EQ(metrics.width, 24.0f);
  EXPECT_EQ(metrics.height, 20.0f);

  metrics = HeadlessRenderer::MeasureText("ab\ncde\nf", 16);
  EXPECT_EQ(metrics.width, 24.0f);
  EXPECT_EQ(metrics.height, 60.0f);

  // a trailing newline starts another (empty) line
  metrics = HeadlessRenderer::MeasureText("a\n", 16);
  EXPECT_EQ(metrics.width, 8.0f);
  EXPECT_EQ(metrics.height, 40.0f);

  metrics = HeadlessRenderer::MeasureText("abcd", 10);
  EXPECT_EQ(metrics.width, 20.0f);
  EXPECT_EQ(metrics.height, 12.5f);
  EXPECT_EQ(metrics.ascent, 10.0f);
}

TEST(Headless, TextMetricsCountCodePointsNotBytes) {
  // e-acute (2 bytes), euro sign (3 bytes), a grinning face (4 bytes)
  const auto metrics = HeadlessRenderer::MeasureText("\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80", 16);
  EXPECT_EQ(metrics.width, 24.0f);
}

TEST(Headless, MeasuringThroughTheAbiMatchesTheStaticMeasure) {
  HeadlessRenderer renderer;
  const auto abi = renderer.Abi();
  const std::string text = "some text\nmore";
  KuraTextMetrics via_abi{};
  ASSERT_EQ(abi.ops->measure_text(abi.self, text.data(), text.size(), KuraTextStyle{14}, &via_abi), KURA_RENDERER_OK);
  const auto direct = HeadlessRenderer::MeasureText(text, 14);
  EXPECT_EQ(via_abi.width, direct.width);
  EXPECT_EQ(via_abi.height, direct.height);
  EXPECT_EQ(via_abi.ascent, direct.ascent);
}

TEST(Headless, MeasureTextOfABadSizeIsZero) {
  const auto metrics = HeadlessRenderer::MeasureText("abc", 0);
  EXPECT_EQ(metrics.width, 0.0f);
  EXPECT_EQ(metrics.height, 0.0f);
}

// ---- no framebuffer -------------------------------------------------------

TEST(Headless, WithoutPixelsItStillRecordsAndTracksState) {
  HeadlessRenderer renderer(Options(false));
  Drawer draw(renderer);
  draw.Begin(1000, 1000);
  draw.Push({0, 0, 10, 10});
  draw.Fill({0, 0, 5, 5}, kRed);
  draw.Line({0, 0}, {9, 9}, kRed);
  draw.Stroke({0, 0, 5, 5}, kRed);
  EXPECT_EQ(renderer.GetClipDepth(), 1u);
  draw.Pop();
  draw.End();
  EXPECT_TRUE(renderer.GetPixels().empty());
  EXPECT_EQ(renderer.GetCommands().size(), 5u);
  EXPECT_EQ(renderer.PixelAt(0, 0), kClearPixel);
  EXPECT_EQ(renderer.CountPixels(kRedPixel), 0u);
  EXPECT_TRUE(renderer.ToPpm().empty());
  EXPECT_EQ(renderer.GetWidth(), 1000u);
}

// ---- output ---------------------------------------------------------------

TEST(Headless, PixelAtOutOfBoundsIsTransparent) {
  HeadlessRenderer renderer(Options(true, kRed));
  Drawer draw(renderer);
  draw.Begin(2, 2);
  draw.End();
  EXPECT_EQ(renderer.PixelAt(1, 1), kRedPixel);
  EXPECT_EQ(renderer.PixelAt(2, 0), kClearPixel);
  EXPECT_EQ(renderer.PixelAt(0, 2), kClearPixel);
  EXPECT_EQ(renderer.PixelAt(100000, 100000), kClearPixel);
}

TEST(Headless, PixelsAreRowMajorRgbaFromTheTopLeft) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  draw.Begin(3, 2);
  draw.Fill({1, 1, 1, 1}, {0.2f, 0.4f, 0.6f, 1.0f});
  draw.End();
  const auto& pixels = renderer.GetPixels();
  ASSERT_EQ(pixels.size(), 24u);
  // (1, 1) is the fifth pixel
  EXPECT_EQ(pixels[16], 51);
  EXPECT_EQ(pixels[17], 102);
  EXPECT_EQ(pixels[18], 153);
  EXPECT_EQ(pixels[19], 255);
  EXPECT_EQ(pixels[0], 0);
}

TEST(Headless, PpmCompositesOverTheBackground) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  draw.Begin(3, 1);
  draw.Fill({0, 0, 1, 1}, kRed);
  draw.Fill({1, 0, 1, 1}, {1.0f, 0.0f, 0.0f, 0.5f});
  draw.End();

  const std::string header = "P6\n3 1\n255\n";
  const auto white = renderer.ToPpm();
  ASSERT_EQ(white.size(), header.size() + 9);
  EXPECT_TRUE(white.compare(0, header.size(), header) == 0);
  const auto byte = [&](const std::string& ppm, const size_t idx) { return static_cast<int>(static_cast<unsigned char>(ppm[header.size() + idx])); };
  // opaque red
  EXPECT_EQ(byte(white, 0), 255);
  EXPECT_EQ(byte(white, 1), 0);
  EXPECT_EQ(byte(white, 2), 0);
  // half red over white
  EXPECT_EQ(byte(white, 3), 255);
  EXPECT_EQ(byte(white, 4), 127);
  EXPECT_EQ(byte(white, 5), 127);
  // nothing over white
  EXPECT_EQ(byte(white, 6), 255);
  EXPECT_EQ(byte(white, 8), 255);

  const auto black = renderer.ToPpm({0, 0, 0, 255});
  EXPECT_EQ(byte(black, 6), 0);
  EXPECT_EQ(byte(black, 3), 128);
  EXPECT_EQ(byte(black, 4), 0);
}

TEST(Headless, CommandDumpHasAStableFormat) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  draw.Begin(40, 20);
  draw.Fill({0, 0, 10.5f, 4}, kRed);
  draw.Stroke({-0.0f, 1.25f, 3, 3}, {0.0f, 0.0f, 1.0f, 0.5f});
  draw.Line({1, 2}, {3, 4}, kGreen);
  draw.Push({2, 2, 20, 10});
  draw.Text({4, 5}, "a \"b\"\n\\c\t\x01", 8, kWhite);
  draw.Pop();
  draw.End();
  EXPECT_EQ(renderer.DumpCommands(),
                 "frame 40x20\n"
                 "fill-rect 0 0 10.5 4 #ff0000ff\n"
                 "stroke-rect 0 1.25 3 3 #0000ff80\n"
                 "line 1 2 3 4 #00ff00ff\n"
                 "push-clip 2 2 20 10\n"
                 "text 4 5 8 #ffffffff \"a \\\"b\\\"\\n\\\\c\\x09\\x01\"\n"
                 "pop-clip\n");
}

TEST(Headless, SameCallsSameBytes) {
  const auto scene = [](HeadlessRenderer& renderer) {
    Drawer draw(renderer);
    draw.Begin(24, 24);
    draw.Fill({1.3f, 2.7f, 9.9f, 8.1f}, {0.3f, 0.6f, 0.9f, 0.45f});
    draw.Push({4, 4, 12, 12});
    draw.Line({0, 0}, {23, 17}, {1.0f, 0.5f, 0.25f, 0.7f});
    draw.Stroke({3.5f, 3.5f, 14, 14}, {0.0f, 0.0f, 0.0f, 0.3f});
    draw.Pop();
    draw.Text({1, 1}, "same", 10, kBlue);
    draw.End();
  };
  HeadlessRenderer a;
  HeadlessRenderer b;
  scene(a);
  scene(b);
  EXPECT_TRUE(a.GetPixels() == b.GetPixels());
  EXPECT_EQ(a.DumpCommands(), b.DumpCommands());
  EXPECT_TRUE(a.GetCommands() == b.GetCommands());
}

// ---- the abi handles ------------------------------------------------------

TEST(Headless, TheBorrowedHandleDoesNotOwn) {
  HeadlessRenderer renderer;
  const auto abi = renderer.Abi();
  EXPECT_EQ(abi.ops->abi_version, KURA_RENDERER_ABI_VERSION);
  EXPECT_EQ(abi.ops->struct_size, sizeof(KuraRendererOps));
  abi.ops->destroy(abi.self);
  // still alive
  Drawer draw(renderer);
  draw.Begin(4, 4);
  draw.Fill({0, 0, 4, 4}, kRed);
  draw.End();
  EXPECT_EQ(renderer.CountPixels(kRedPixel), 16u);
}

TEST(Headless, TheOwnedHandleCanBeMadeAndDestroyed) {
  KuraRenderer renderer{};
  ASSERT_EQ(kura_headless_renderer_create(nullptr, &renderer), KURA_RENDERER_OK);
  ASSERT_TRUE(renderer.ops != nullptr);
  EXPECT_EQ(renderer.ops->begin_frame(renderer.self, 4, 4), KURA_RENDERER_OK);
  EXPECT_EQ(renderer.ops->fill_rect(renderer.self, KuraRect{0, 0, 2, 2}, kRed), KURA_RENDERER_OK);
  EXPECT_EQ(renderer.ops->end_frame(renderer.self), KURA_RENDERER_OK);
  renderer.ops->destroy(renderer.self);
}

TEST(Headless, CreatingNeedsSomewhereToPutTheRenderer) {
  EXPECT_EQ(kura_headless_renderer_create(nullptr, nullptr), KURA_RENDERER_ERR_INVALID_ARG);
}

TEST(Headless, TwoRenderersDontShareState) {
  HeadlessRenderer a;
  HeadlessRenderer b;
  Drawer da(a);
  Drawer db(b);
  da.Begin(4, 4);
  db.Begin(8, 8);
  da.Fill({0, 0, 4, 4}, kRed);
  EXPECT_EQ(a.GetWidth(), 4u);
  EXPECT_EQ(b.GetWidth(), 8u);
  EXPECT_TRUE(b.GetCommands().empty());
  da.End();
  EXPECT_TRUE(!a.InFrame());
  EXPECT_TRUE(b.InFrame());
  db.End();
}
