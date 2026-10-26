// Golden tests: a small scene, drawn through the ABI, whose commands and
// pixels are checked against files in golden/. They're what to look at in a
// diff when the headless renderer changes what it does.
#include "golden.h"
#include "headless_util.h"

using namespace kura::renderer;

namespace {
using namespace kura::test;

constexpr KuraColor kPanel = {0.2f, 0.2f, 0.25f, 1.0f};
constexpr KuraColor kShadow = {0.0f, 0.0f, 0.0f, 0.5f};

// A button in a panel: the kind of thing a view lowers to. 40x12 pixels.
void DrawButton(HeadlessRenderer& renderer) {
  Drawer draw(renderer);
  draw.Begin(40, 12);
  draw.Fill({0, 0, 40, 12}, kPanel);
  draw.Fill({5, 3, 32, 8}, kShadow);
  draw.Fill({4, 2, 32, 8}, kBlue);
  draw.Stroke({4, 2, 32, 8}, kWhite);
  draw.Push({6, 3, 28, 6});
  draw.Text({8, 3}, "Go", 8, kWhite);
  draw.Line({6, 8}, {33, 8}, kYellow);
  draw.Pop();
  draw.End();
}

const std::map<char, Rgba8> kPalette = {
    {'p', {51, 51, 64, 255}},   // the panel
    {'s', {25, 25, 32, 255}},   // the shadow over the panel (kShadow, 50%)
    {'B', kBluePixel},
    {'W', kWhitePixel},
    {'Y', kYellowPixel},
};
}  // namespace

using kura::test::MatchesGolden;
TEST(HeadlessGolden, ButtonCommands) {
  HeadlessRenderer renderer;
  DrawButton(renderer);
  EXPECT_TRUE(MatchesGolden("button.commands.txt", renderer.DumpCommands()));
}

TEST(HeadlessGolden, ButtonPixels) {
  HeadlessRenderer renderer;
  DrawButton(renderer);
  EXPECT_TRUE(MatchesGolden("button.pixels.txt", AsciiArt(renderer, kPalette)));
}

TEST(HeadlessGolden, ButtonPixelsAreRepeatable) {
  HeadlessRenderer renderer;
  DrawButton(renderer);
  const auto first = renderer.GetPixels();
  DrawButton(renderer);
  EXPECT_TRUE(first == renderer.GetPixels());
}
