#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "headless_util.h"
#include "text/text.h"

namespace kura::test {
namespace {
using renderer::HeadlessOptions;
using renderer::HeadlessRenderer;

auto Style(const float size, const std::string& family) -> KuraTextStyle {
  return KuraTextStyle{size, family.empty() ? nullptr : family.data(), family.size()};
}

auto InkPixels(const HeadlessRenderer& renderer) -> uint64_t {
  const auto& pixels = renderer.GetPixels();
  uint64_t count = 0;
  for (size_t idx = 3; idx < pixels.size(); idx += 4)
    count += pixels[idx] != 0;
  return count;
}

// Without real fonts, text is the fixed model and draws no pixels, whatever the family.
TEST(HeadlessText, FixedModelIgnoresTheFamily) {
  HeadlessRenderer renderer{};
  Drawer draw(renderer);
  draw.Begin(64, 32);
  const std::string text = "Hello";
  KuraTextMetrics plain{};
  KuraTextMetrics with_family{};
  ASSERT_EQ(draw.Ops().measure_text(draw.Self(), text.data(), text.size(), Style(16, ""), &plain), KURA_RENDERER_OK);
  ASSERT_EQ(draw.Ops().measure_text(draw.Self(), text.data(), text.size(), Style(16, "Inter, sans-serif"),
                                    &with_family),
            KURA_RENDERER_OK);
  EXPECT_EQ(plain.width, 40.0f);
  EXPECT_EQ(plain.height, 20.0f);
  EXPECT_EQ(with_family.width, plain.width);
  EXPECT_EQ(with_family.height, plain.height);

  ASSERT_EQ(draw.Ops().draw_text(draw.Self(), {2, 2}, text.data(), text.size(), Style(16, "Inter"), kRed),
            KURA_RENDERER_OK);
  draw.End();
  EXPECT_EQ(InkPixels(renderer), 0u);
}

TEST(HeadlessText, RecordsTheFamilyOnlyWhenThereIsOne) {
  HeadlessRenderer renderer{};
  Drawer draw(renderer);
  draw.Begin(64, 32);
  const std::string text = "Hi";
  ASSERT_EQ(draw.Ops().draw_text(draw.Self(), {0, 0}, text.data(), text.size(), Style(8, ""), kRed), KURA_RENDERER_OK);
  ASSERT_EQ(draw.Ops().draw_text(draw.Self(), {0, 0}, text.data(), text.size(), Style(8, "Inter"), kRed),
            KURA_RENDERER_OK);
  draw.End();
  ASSERT_EQ(renderer.GetCommands().size(), 2u);
  EXPECT_EQ(renderer.GetCommands()[0].family, "");
  EXPECT_EQ(renderer.GetCommands()[1].family, "Inter");
  EXPECT_NE(renderer.GetCommands()[0], renderer.GetCommands()[1]);

  const auto dump = renderer.DumpCommands();
  EXPECT_NE(dump.find("\"Hi\"\n"), std::string::npos);
  EXPECT_NE(dump.find("\"Hi\" font \"Inter\"\n"), std::string::npos);
}

TEST(HeadlessText, ANullFamilyWithALengthIsInvalid) {
  HeadlessRenderer renderer{};
  Drawer draw(renderer);
  draw.Begin(8, 8);
  KuraTextStyle bad{12, nullptr, 3};
  KuraTextMetrics metrics{};
  EXPECT_EQ(draw.Ops().draw_text(draw.Self(), {0, 0}, "a", 1, bad, kRed), KURA_RENDERER_ERR_INVALID_ARG);
  EXPECT_EQ(draw.Ops().measure_text(draw.Self(), "a", 1, bad, &metrics), KURA_RENDERER_ERR_INVALID_ARG);
}

class ShapedHeadlessText : public ::testing::Test {
 protected:
  void SetUp() override {
    text::FontLibrary library{};
    if (library.Find("") == nullptr)
      GTEST_SKIP() << "no system font";
  }

  static auto Options() -> HeadlessOptions {
    HeadlessOptions options{};
    options.shaped_text = true;
    return options;
  }
};

TEST_F(ShapedHeadlessText, MeasuresWithTheFont) {
  HeadlessRenderer renderer{Options()};
  Drawer draw(renderer);
  text::FontLibrary library{};
  const auto font = library.Find("");
  const std::string text = "Kura";
  KuraTextMetrics metrics{};
  ASSERT_EQ(draw.Ops().measure_text(draw.Self(), text.data(), text.size(), Style(20, ""), &metrics), KURA_RENDERER_OK);
  const auto expected = font->Measure(text, 20);
  EXPECT_EQ(metrics.width, expected.width);
  EXPECT_EQ(metrics.height, expected.height);
  EXPECT_EQ(metrics.ascent, expected.ascent);
  // and not the fixed model
  EXPECT_NE(metrics.width, 40.0f);
}

TEST_F(ShapedHeadlessText, ADifferentFamilyIsTheFallbackWhenItIsMissing) {
  HeadlessRenderer renderer{Options()};
  Drawer draw(renderer);
  const std::string text = "Fallback";
  KuraTextMetrics a{};
  KuraTextMetrics b{};
  ASSERT_EQ(draw.Ops().measure_text(draw.Self(), text.data(), text.size(), Style(16, ""), &a), KURA_RENDERER_OK);
  ASSERT_EQ(draw.Ops().measure_text(draw.Self(), text.data(), text.size(), Style(16, "No Such Family 4f1c"), &b),
            KURA_RENDERER_OK);
  EXPECT_EQ(a.width, b.width);
}

TEST_F(ShapedHeadlessText, DrawsGlyphsInTheColorAndNowhereElse) {
  HeadlessRenderer renderer{Options()};
  Drawer draw(renderer);
  draw.Begin(120, 40);
  const std::string text = "Hello";
  ASSERT_EQ(draw.Ops().draw_text(draw.Self(), {4, 4}, text.data(), text.size(), Style(24, ""), kRed),
            KURA_RENDERER_OK);
  draw.End();
  EXPECT_GT(InkPixels(renderer), 20u);

  const auto metrics = HeadlessRenderer::MeasureText(text, 24);
  (void)metrics;
  // every drawn pixel is red (the channel is kept and alpha is the glyph's coverage)
  const auto& pixels = renderer.GetPixels();
  for (size_t idx = 0; idx < pixels.size(); idx += 4) {
    if (pixels[idx + 3] != 0) {
      EXPECT_EQ(pixels[idx + 0], 255);
      EXPECT_EQ(pixels[idx + 1], 0);
      EXPECT_EQ(pixels[idx + 2], 0);
    }
  }
  // nothing is above the box's top-left corner
  for (uint32_t y = 0; y < 4; y++) {
    for (uint32_t x = 0; x < 120; x++)
      EXPECT_EQ(renderer.PixelAt(x, y).a, 0) << x << "," << y;
  }
}

TEST_F(ShapedHeadlessText, IsClipped) {
  HeadlessRenderer renderer{Options()};
  Drawer draw(renderer);
  draw.Begin(120, 40);
  draw.Push({0, 0, 30, 40});
  const std::string text = "Wide text that runs past the clip";
  ASSERT_EQ(draw.Ops().draw_text(draw.Self(), {0, 4}, text.data(), text.size(), Style(20, ""), kBlue),
            KURA_RENDERER_OK);
  draw.Pop();
  draw.End();
  EXPECT_GT(InkPixels(renderer), 0u);
  for (uint32_t y = 0; y < 40; y++) {
    for (uint32_t x = 30; x < 120; x++)
      EXPECT_EQ(renderer.PixelAt(x, y).a, 0) << x << "," << y;
  }
}

TEST_F(ShapedHeadlessText, LaterLinesAreLowerAndASpaceDrawsNothing) {
  HeadlessRenderer renderer{Options()};
  Drawer draw(renderer);
  draw.Begin(80, 80);
  const std::string text = "I\n \nI";
  ASSERT_EQ(draw.Ops().draw_text(draw.Self(), {0, 0}, text.data(), text.size(), Style(20, ""), kWhite),
            KURA_RENDERER_OK);
  draw.End();

  text::FontLibrary library{};
  const auto line = library.Find("")->GetMetrics(20).line_height;
  // ink in the first and the third line, none in the second (the line of a space)
  const auto ink_between = [&](const float top, const float bottom) {
    uint64_t count = 0;
    for (auto y = static_cast<uint32_t>(top); y < static_cast<uint32_t>(bottom); y++) {
      for (uint32_t x = 0; x < 80; x++)
        count += renderer.PixelAt(x, y).a != 0;
    }
    return count;
  };
  EXPECT_GT(ink_between(0, line), 0u);
  EXPECT_EQ(ink_between(line + 1, 2 * line - 1), 0u);
  EXPECT_GT(ink_between(2 * line, 3 * line), 0u);
}
}  // namespace
}  // namespace kura::test
