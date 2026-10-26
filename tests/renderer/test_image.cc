// Tests for the PNG / JPEG output, and visual tests: scenes compared with the
// golden images in golden/ (see golden_image.h for the environment variables
// that rewrite them and save pictures to look at).
#include <filesystem>
#include <string>

#include "golden_image.h"
#include "headless_util.h"
#include "renderer/renderer_image.h"

using namespace kura::test;
using namespace kura::renderer;

namespace {
constexpr KuraColor kPanel = {0.15f, 0.17f, 0.22f, 1.0f};
constexpr KuraColor kTranslucentRed = {1.0f, 0.1f, 0.1f, 0.6f};
constexpr KuraColor kTranslucentBlue = {0.1f, 0.3f, 1.0f, 0.6f};

// 96x64: overlapping translucent fills, outlines, lines at several slopes, and
// nested clips that cut a diagonal short.
void DrawSampler(HeadlessRenderer& renderer) {
  Drawer draw(renderer);
  draw.Begin(96, 64);
  draw.Fill({0, 0, 96, 64}, kPanel);
  draw.Fill({6, 6, 36, 28}, kTranslucentRed);
  draw.Fill({22, 18, 36, 28}, kTranslucentBlue);
  draw.Stroke({6, 6, 36, 28}, kWhite);
  draw.Stroke({22, 18, 36, 28}, kYellow);
  draw.Line({64, 6}, {90, 6}, kWhite);
  draw.Line({64, 6}, {90, 14}, kGreen);
  draw.Line({64, 6}, {80, 30}, kGreen);
  draw.Line({64, 6}, {64, 30}, kGreen);
  draw.Push({60, 34, 30, 24});
  draw.Fill({56, 30, 40, 32}, {1.0f, 1.0f, 1.0f, 0.15f});
  draw.Push({70, 40, 40, 40});
  draw.Line({58, 32}, {94, 62}, kRed);
  draw.Fill({76, 46, 8, 8}, kBlue);
  draw.Pop();
  draw.Line({58, 62}, {94, 32}, kYellow);
  draw.Pop();
  draw.Stroke({60, 34, 30, 24}, {1.0f, 1.0f, 1.0f, 0.5f});
  draw.End();
}

auto Sampler() -> Image {
  HeadlessRenderer renderer;
  DrawSampler(renderer);
  return ImageFromRenderer(renderer);
}

auto Solid(const uint32_t width, const uint32_t height, const Rgba8& color) -> Image {
  Image image;
  image.width = width;
  image.height = height;
  for (uint32_t idx = 0; idx < width * height; idx++) {
    image.rgba.push_back(color.r);
    image.rgba.push_back(color.g);
    image.rgba.push_back(color.b);
    image.rgba.push_back(color.a);
  }
  return image;
}

auto TempPath(const std::string& name) -> std::string {
  return (std::filesystem::temp_directory_path() / ("kura-image-test-" + name)).string();
}
}  // namespace

// ---- the golden -----------------------------------------------------------

TEST(Image, SamplerMatchesItsGolden) {
  EXPECT_TRUE(MatchesGoldenImage("sampler", Sampler()));
}

TEST(Image, ButtonMatchesItsGolden) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  draw.Begin(40, 12);
  draw.Fill({0, 0, 40, 12}, kPanel);
  draw.Fill({5, 3, 32, 8}, {0.0f, 0.0f, 0.0f, 0.5f});
  draw.Fill({4, 2, 32, 8}, kBlue);
  draw.Stroke({4, 2, 32, 8}, kWhite);
  draw.End();
  EXPECT_TRUE(MatchesGoldenImage("button", ImageFromRenderer(renderer)));
}

// ---- PNG ------------------------------------------------------------------

TEST(Image, PngRoundTripsExactlyWithAlpha) {
  const auto image = Sampler();
  const auto bytes = EncodePng(image);
  ASSERT_TRUE(bytes.has_value());
  // the PNG signature
  ASSERT_TRUE(bytes->size() > 8);
  const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  for (auto idx = 0; idx < 8; idx++)
    EXPECT_EQ((*bytes)[idx], signature[idx]);

  const auto decoded = DecodePng(*bytes);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->width, 96u);
  EXPECT_EQ(decoded->height, 64u);
  EXPECT_TRUE(decoded->rgba == image.rgba);
}

TEST(Image, PngKeepsTransparencyAndPartialAlpha) {
  HeadlessRenderer renderer;
  Drawer draw(renderer);
  draw.Begin(3, 1);
  draw.Fill({1, 0, 1, 1}, {1.0f, 0.0f, 0.0f, 0.5f});
  draw.Fill({2, 0, 1, 1}, kBlue);
  draw.End();
  const auto decoded = DecodePng(*EncodePng(ImageFromRenderer(renderer)));
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->PixelAt(0, 0), kClearPixel);
  EXPECT_EQ(decoded->PixelAt(1, 0), (Rgba8{255, 0, 0, 128}));
  EXPECT_EQ(decoded->PixelAt(2, 0), kBluePixel);
}

TEST(Image, PngEncodingIsRepeatable) {
  EXPECT_TRUE(*EncodePng(Sampler()) == *EncodePng(Sampler()));
}

TEST(Image, PngOfAOnePixelImageWorks) {
  const auto decoded = DecodePng(*EncodePng(Solid(1, 1, {10, 20, 30, 40})));
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->PixelAt(0, 0), (Rgba8{10, 20, 30, 40}));
}

TEST(Image, PngRefusesImagesThatAreNotImages) {
  EXPECT_TRUE(!EncodePng(Image{}).has_value());
  Image wrong_size = Solid(4, 4, kRedPixel);
  wrong_size.rgba.pop_back();
  EXPECT_TRUE(!EncodePng(wrong_size).has_value());
  Image no_height = Solid(4, 4, kRedPixel);
  no_height.height = 0;
  EXPECT_TRUE(!EncodePng(no_height).has_value());
}

TEST(Image, PngDecodeRefusesGarbage) {
  EXPECT_TRUE(!DecodePng({}).has_value());
  EXPECT_TRUE(!DecodePng({1, 2, 3, 4, 5, 6, 7, 8, 9, 10}).has_value());
  auto bytes = *EncodePng(Sampler());
  bytes.resize(bytes.size() / 2);
  EXPECT_TRUE(!DecodePng(bytes).has_value());
  // a JPEG isn't a PNG
  EXPECT_TRUE(!DecodePng(*EncodeJpeg(Sampler())).has_value());
}

// ---- JPEG -----------------------------------------------------------------

TEST(Image, JpegHasItsMarkersAndSize) {
  const auto bytes = EncodeJpeg(Sampler());
  ASSERT_TRUE(bytes.has_value());
  ASSERT_TRUE(bytes->size() > 4);
  EXPECT_EQ((*bytes)[0], 0xFF);
  EXPECT_EQ((*bytes)[1], 0xD8);
  EXPECT_EQ((*bytes)[bytes->size() - 2], 0xFF);
  EXPECT_EQ((*bytes)[bytes->size() - 1], 0xD9);
  const auto decoded = DecodeJpeg(*bytes);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->width, 96u);
  EXPECT_EQ(decoded->height, 64u);
}

TEST(Image, JpegIsCloseToTheOriginal) {
  // solid areas come back within a few levels, even though JPEG is lossy
  Image image = Solid(32, 32, {40, 160, 220, 255});
  const auto decoded = DecodeJpeg(*EncodeJpeg(image, 95));
  ASSERT_TRUE(decoded.has_value());
  const auto diff = Compare(image, *decoded, 6);
  EXPECT_EQ(diff.differing_pixels, 0u);
  EXPECT_EQ(decoded->PixelAt(16, 16).a, 255);
}

TEST(Image, JpegQualityTradesSizeForFidelity) {
  const auto image = Sampler();
  const auto low = EncodeJpeg(image, 10);
  const auto high = EncodeJpeg(image, 100);
  ASSERT_TRUE(low.has_value() && high.has_value());
  EXPECT_TRUE(low->size() < high->size());
  // and the higher quality is the nearer to the original (flattened over white)
  Image flat = image;
  for (size_t idx = 0; idx < flat.rgba.size(); idx += 4)
    flat.rgba[idx + 3] = 255;  // the sampler is opaque
  EXPECT_TRUE(Compare(flat, *DecodeJpeg(*high)).max_delta <= Compare(flat, *DecodeJpeg(*low)).max_delta);
}

TEST(Image, JpegFlattensTransparencyOverTheBackground) {
  const auto clear = Solid(16, 16, kClearPixel);
  const auto on_white = DecodeJpeg(*EncodeJpeg(clear, 95, {255, 255, 255, 255}));
  const auto on_black = DecodeJpeg(*EncodeJpeg(clear, 95, {0, 0, 0, 255}));
  ASSERT_TRUE(on_white.has_value() && on_black.has_value());
  EXPECT_TRUE(on_white->PixelAt(8, 8).g >= 250);
  EXPECT_TRUE(on_black->PixelAt(8, 8).g <= 5);
}

TEST(Image, JpegQualityIsClamped) {
  EXPECT_TRUE(EncodeJpeg(Solid(8, 8, kRedPixel), 0).has_value());
  EXPECT_TRUE(EncodeJpeg(Solid(8, 8, kRedPixel), 1000).has_value());
  EXPECT_TRUE(EncodeJpeg(Solid(8, 8, kRedPixel), -5).has_value());
}

TEST(Image, JpegOfAOnePixelImageWorks) {
  const auto decoded = DecodeJpeg(*EncodeJpeg(Solid(1, 1, kRedPixel), 95));
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->width, 1u);
  EXPECT_TRUE(decoded->PixelAt(0, 0).r >= 245);
}

TEST(Image, JpegEncodingIsRepeatable) {
  EXPECT_TRUE(*EncodeJpeg(Sampler(), 80) == *EncodeJpeg(Sampler(), 80));
}

TEST(Image, JpegRefusesImagesThatAreNotImages) {
  EXPECT_TRUE(!EncodeJpeg(Image{}).has_value());
  Image wrong_size = Solid(4, 4, kRedPixel);
  wrong_size.rgba.pop_back();
  EXPECT_TRUE(!EncodeJpeg(wrong_size).has_value());
}

TEST(Image, JpegDecodeRefusesGarbage) {
  EXPECT_TRUE(!DecodeJpeg({}).has_value());
  EXPECT_TRUE(!DecodeJpeg({1, 2, 3, 4, 5, 6, 7, 8, 9, 10}).has_value());
  EXPECT_TRUE(!DecodeJpeg({0xFF, 0xD8, 0xFF}).has_value());
  EXPECT_TRUE(!DecodeJpeg(*EncodePng(Sampler())).has_value());
}

TEST(Image, JpegDecodeSurvivesATruncatedFile) {
  // either it refuses, or it gives back an image of the right size: never a crash
  auto bytes = *EncodeJpeg(Sampler());
  bytes.resize(bytes.size() / 2);
  const auto decoded = DecodeJpeg(bytes);
  if (decoded.has_value()) {
    EXPECT_EQ(decoded->width, 96u);
    EXPECT_TRUE(decoded->IsValid());
  }
}

// ---- files ----------------------------------------------------------------

TEST(Image, SavedFilesCanBeReadBack) {
  const auto image = Sampler();
  const auto png = TempPath("saved.png");
  const auto jpg = TempPath("saved.jpg");
  ASSERT_TRUE(SavePng(image, png));
  ASSERT_TRUE(SaveJpeg(image, jpg, 90));
  const auto loaded = LoadPng(png);
  ASSERT_TRUE(loaded.has_value());
  EXPECT_TRUE(loaded->rgba == image.rgba);
  EXPECT_TRUE(std::filesystem::file_size(jpg) > 100);
  std::filesystem::remove(png);
  std::filesystem::remove(jpg);
}

TEST(Image, FileFailuresAreReported) {
  const auto missing = TempPath("no-such-dir/x.png");
  EXPECT_TRUE(!SavePng(Sampler(), missing));
  EXPECT_TRUE(!SaveJpeg(Sampler(), missing));
  EXPECT_TRUE(!LoadPng(missing).has_value());
  EXPECT_TRUE(!SavePng(Image{}, TempPath("empty.png")));
  // a file that isn't a PNG
  const auto text = TempPath("text.png");
  ASSERT_TRUE(SaveJpeg(Sampler(), text));
  EXPECT_TRUE(!LoadPng(text).has_value());
  std::filesystem::remove(text);
}

// ---- comparing ------------------------------------------------------------

TEST(Image, ComparingIdenticalImagesMatches) {
  const auto diff = Compare(Sampler(), Sampler());
  EXPECT_TRUE(diff.IsMatch());
  EXPECT_EQ(diff.differing_pixels, 0u);
  EXPECT_EQ(diff.max_delta, 0);
}

TEST(Image, ComparingFindsOnePixel) {
  const auto a = Solid(8, 8, kRedPixel);
  auto b = a;
  b.rgba[(3 * 8 + 5) * 4 + 1] = 40;
  const auto diff = Compare(a, b);
  EXPECT_TRUE(!diff.IsMatch());
  EXPECT_EQ(diff.differing_pixels, 1u);
  EXPECT_EQ(diff.max_delta, 40);
}

TEST(Image, ComparingHonoursTheTolerance) {
  const auto a = Solid(4, 4, {100, 100, 100, 255});
  const auto b = Solid(4, 4, {103, 98, 100, 255});
  EXPECT_EQ(Compare(a, b, 0).differing_pixels, 16u);
  EXPECT_EQ(Compare(a, b, 2).differing_pixels, 16u);
  EXPECT_TRUE(Compare(a, b, 3).IsMatch());
  EXPECT_EQ(Compare(a, b, 3).max_delta, 3);
}

TEST(Image, ComparingSeesAlphaDifferences) {
  EXPECT_TRUE(!Compare(Solid(2, 2, {1, 2, 3, 255}), Solid(2, 2, {1, 2, 3, 200})).IsMatch());
}

TEST(Image, ComparingDifferentSizesIsNotAMatch) {
  const auto diff = Compare(Solid(4, 4, kRedPixel), Solid(4, 5, kRedPixel));
  EXPECT_TRUE(!diff.same_size);
  EXPECT_TRUE(!diff.IsMatch());
  EXPECT_TRUE(!MakeDiffImage(Solid(4, 4, kRedPixel), Solid(4, 5, kRedPixel)).IsValid());
}

TEST(Image, TheDiffImageMarksOnlyWhatDiffers) {
  const auto a = Solid(4, 4, kBluePixel);
  auto b = a;
  b.rgba[(2 * 4 + 1) * 4] = 255;  // pixel (1, 2) gets a red channel
  const auto diff = MakeDiffImage(a, b);
  ASSERT_TRUE(diff.IsValid());
  EXPECT_EQ(diff.PixelAt(1, 2), kRedPixel);
  EXPECT_NE(diff.PixelAt(0, 0), kRedPixel);
  EXPECT_EQ(diff.PixelAt(0, 0).a, 255);
  uint32_t marked = 0;
  for (uint32_t y = 0; y < 4; y++) {
    for (uint32_t x = 0; x < 4; x++)
      marked += diff.PixelAt(x, y) == kRedPixel ? 1 : 0;
  }
  EXPECT_EQ(marked, 1u);
}

// ---- from the renderer ----------------------------------------------------

TEST(Image, ImageFromTheRendererIsTheFramebuffer) {
  HeadlessRenderer renderer;
  DrawSampler(renderer);
  const auto image = ImageFromRenderer(renderer);
  EXPECT_TRUE(image.IsValid());
  EXPECT_EQ(image.width, renderer.GetWidth());
  EXPECT_TRUE(image.rgba == renderer.GetPixels());
  EXPECT_EQ(image.PixelAt(7, 7), renderer.PixelAt(7, 7));
  EXPECT_EQ(image.PixelAt(1000, 0), kClearPixel);
}

TEST(Image, ARendererWithoutPixelsGivesNoImage) {
  HeadlessOptions options;
  options.rasterize = false;
  HeadlessRenderer renderer(options);
  Drawer draw(renderer);
  draw.Begin(8, 8);
  draw.End();
  const auto image = ImageFromRenderer(renderer);
  EXPECT_TRUE(!image.IsValid());
  EXPECT_TRUE(!EncodePng(image).has_value());
}
