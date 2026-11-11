#include <gtest/gtest.h>

#include <algorithm>

#include "text/text.h"

namespace {
using kura::text::Font;
using kura::text::FontLibrary;

// These need a font on the machine; there is none bundled, by design.
class TextTest : public ::testing::Test {
 protected:
  void SetUp() override {
    font_ = library_.Find("");
    if (font_ == nullptr)
      GTEST_SKIP() << "no system font";
  }

  FontLibrary library_{};
  Font* font_ = nullptr;
};

TEST_F(TextTest, MeasuresWhatItShapes) {
  const auto lines = font_->Shape("Hello", 16);
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0].glyphs.size(), 5u);

  const auto metrics = font_->Measure("Hello", 16);
  EXPECT_FLOAT_EQ(metrics.width, lines[0].width);
  EXPECT_GT(metrics.width, 0.0f);
  EXPECT_EQ(metrics.lines, 1u);
  EXPECT_GT(metrics.ascent, 0.0f);
  EXPECT_GE(metrics.height, metrics.ascent);
}

TEST_F(TextTest, WidthScalesWithSize) {
  const auto small = font_->Measure("Hello, world", 10);
  const auto big = font_->Measure("Hello, world", 40);
  EXPECT_NEAR(big.width / small.width, 4.0f, 0.1f);
}

TEST_F(TextTest, LinesSplitAtNewlines) {
  const auto one = font_->Measure("ab", 16);
  const auto two = font_->Measure("ab\nab", 16);
  EXPECT_EQ(two.lines, 2u);
  EXPECT_FLOAT_EQ(two.height, one.height * 2.0f);
  EXPECT_FLOAT_EQ(two.width, one.width);
  // the widest line is the width
  const auto uneven = font_->Measure("a\nabcd", 16);
  EXPECT_FLOAT_EQ(uneven.width, font_->Measure("abcd", 16).width);
}

TEST_F(TextTest, EmptyTextIsOneLineTall) {
  const auto metrics = font_->Measure("", 16);
  EXPECT_EQ(metrics.lines, 1u);
  EXPECT_EQ(metrics.width, 0.0f);
  EXPECT_GT(metrics.height, 0.0f);
  EXPECT_EQ(font_->Measure("a\n", 16).lines, 2u);
}

TEST_F(TextTest, MeasuringIsDeterministic) {
  const auto a = font_->Measure("Kura text", 13.5f);
  const auto b = font_->Measure("Kura text", 13.5f);
  EXPECT_EQ(a.width, b.width);
  EXPECT_EQ(a.height, b.height);
}

TEST_F(TextTest, MultibyteTextIsShapedByCodePoint) {
  // "é" as two bytes is one glyph, not two
  const auto lines = font_->Shape("\xC3\xA9", 16);
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0].glyphs.size(), 1u);
}

TEST_F(TextTest, RasterizesGlyphs) {
  const auto lines = font_->Shape("A", 24);
  ASSERT_EQ(lines[0].glyphs.size(), 1u);
  const auto bitmap = font_->Rasterize(lines[0].glyphs[0].id, 24);
  ASSERT_GT(bitmap.width, 0);
  ASSERT_GT(bitmap.height, 0);
  EXPECT_EQ(bitmap.alpha.size(), static_cast<size_t>(bitmap.width) * static_cast<size_t>(bitmap.height));
  EXPECT_TRUE(std::any_of(bitmap.alpha.begin(), bitmap.alpha.end(), [](const uint8_t v) { return v > 0; }));
  // an 'A' stands on the baseline
  EXPECT_GT(bitmap.top, 0);
}

TEST_F(TextTest, SpaceHasNoBitmapButAnAdvance) {
  const auto lines = font_->Shape(" ", 16);
  ASSERT_EQ(lines[0].glyphs.size(), 1u);
  EXPECT_GT(lines[0].glyphs[0].x_advance, 0.0f);
  EXPECT_EQ(font_->Rasterize(lines[0].glyphs[0].id, 16).width, 0);
}

TEST_F(TextTest, AMissingFamilyFallsBackToTheSystemFont) {
  const auto missing = library_.Find("No Such Family 4f1c");
  ASSERT_NE(missing, nullptr);
  EXPECT_TRUE(library_.IsFallback("No Such Family 4f1c"));
  EXPECT_FALSE(library_.IsFallback(""));
  EXPECT_EQ(missing, library_.Find(""));
  EXPECT_GT(missing->Measure("text", 16).width, 0.0f);
}

TEST_F(TextTest, AFamilyListUsesTheFirstThatIsInstalled) {
  // the default is installed under its own name, so a list that leads with a family that isn't, and then
  // names it, is that font and not a stand-in
  const auto family = font_->GetFamily();
  ASSERT_FALSE(family.empty());
  const auto found = library_.Find("\"No Such Family 4f1c\", '" + family + "'");
  ASSERT_NE(found, nullptr);
  EXPECT_FALSE(library_.IsFallback("\"No Such Family 4f1c\", '" + family + "'"));
  EXPECT_EQ(found->GetFamily(), family);
}

TEST_F(TextTest, AFileRegisteredForAFamilyIsThatFamily) {
  FontLibrary other{};
  other.AddFile("My Face", font_->GetPath());
  const auto found = other.Find("my face");
  ASSERT_NE(found, nullptr);
  EXPECT_FALSE(other.IsFallback("my face"));
  EXPECT_EQ(found->GetPath(), font_->GetPath());
}
}  // namespace
