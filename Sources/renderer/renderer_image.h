#ifndef KURA_RENDERER_IMAGE_H
#define KURA_RENDERER_IMAGE_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "renderer/renderer_headless.h"

namespace kura::renderer {
struct Image {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> rgba{};

  // True when there is at least one pixel and `rgba` is exactly that big.
  auto IsValid() const -> bool;
  auto PixelAt(uint32_t x, uint32_t y) const -> Rgba8;
};

// A copy of the renderer's framebuffer. Not valid when the renderer isn't
// rasterizing or has no frame yet.
auto ImageFromRenderer(const HeadlessRenderer& renderer) -> Image;

// The image as a PNG, with its alpha. Empty if the image isn't valid.
auto EncodePng(const Image& image) -> std::optional<std::vector<uint8_t>>;

// The image as a JPEG. JPEG has no alpha, so it's composited over `background`
// first. `quality` is 1 to 100. Empty if the image isn't valid.
auto EncodeJpeg(const Image& image, int quality = 90, const Rgba8& background = {255, 255, 255, 255})
    -> std::optional<std::vector<uint8_t>>;

// Empty if the bytes aren't a PNG / JPEG that can be read. A JPEG comes back
// opaque.
auto DecodePng(const std::vector<uint8_t>& bytes) -> std::optional<Image>;
auto DecodeJpeg(const std::vector<uint8_t>& bytes) -> std::optional<Image>;

auto SavePng(const Image& image, const std::string& path) -> bool;
auto SaveJpeg(const Image& image, const std::string& path, int quality = 90,
              const Rgba8& background = {255, 255, 255, 255}) -> bool;
auto LoadPng(const std::string& path) -> std::optional<Image>;

struct ImageDiff {
  // False if the images differ in size (nothing else is filled in then).
  bool same_size = true;
  // How many pixels have a channel that differs by more than the tolerance.
  uint64_t differing_pixels = 0;
  // The largest difference in any channel of any pixel.
  uint8_t max_delta = 0;

  auto IsMatch() const -> bool {
    return same_size && differing_pixels == 0;
  }
};

auto Compare(const Image& expected, const Image& actual, uint8_t tolerance = 0) -> ImageDiff;

// A picture of where two same-size images differ: red where they do (by more
// than the tolerance), and a faded copy of `expected` everywhere else. Invalid
// if the sizes differ.
auto MakeDiffImage(const Image& expected, const Image& actual, uint8_t tolerance = 0) -> Image;
}  // namespace kura::renderer

#endif  // KURA_RENDERER_IMAGE_H
