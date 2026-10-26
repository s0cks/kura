#include "renderer/renderer_image.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <jpeglib.h>
#include <png.h>
#include <setjmp.h>
#include <stdio.h>

namespace kura::renderer {
auto Image::IsValid() const -> bool {
  return width > 0 && height > 0 && rgba.size() == static_cast<size_t>(width) * height * 4;
}

auto Image::PixelAt(const uint32_t x, const uint32_t y) const -> Rgba8 {
  if (x >= width || y >= height || !IsValid())
    return Rgba8{};
  const auto* pixel = &rgba[(static_cast<size_t>(y) * width + x) * 4];
  return Rgba8{pixel[0], pixel[1], pixel[2], pixel[3]};
}

auto ImageFromRenderer(const HeadlessRenderer& renderer) -> Image {
  Image image;
  image.width = renderer.GetWidth();
  image.height = renderer.GetHeight();
  image.rgba = renderer.GetPixels();
  return image;
}

auto EncodePng(const Image& image) -> std::optional<std::vector<uint8_t>> {
  if (!image.IsValid())
    return std::nullopt;
  png_image png{};
  png.version = PNG_IMAGE_VERSION;
  png.width = image.width;
  png.height = image.height;
  png.format = PNG_FORMAT_RGBA;

  png_alloc_size_t size = 0;
  if (!png_image_write_to_memory(&png, nullptr, &size, 0, image.rgba.data(), 0, nullptr)) {
    png_image_free(&png);
    return std::nullopt;
  }
  std::vector<uint8_t> bytes(size);
  if (!png_image_write_to_memory(&png, bytes.data(), &size, 0, image.rgba.data(), 0, nullptr)) {
    png_image_free(&png);
    return std::nullopt;
  }
  bytes.resize(size);
  return bytes;
}

auto DecodePng(const std::vector<uint8_t>& bytes) -> std::optional<Image> {
  if (bytes.empty())
    return std::nullopt;
  png_image png{};
  png.version = PNG_IMAGE_VERSION;
  if (!png_image_begin_read_from_memory(&png, bytes.data(), bytes.size())) {
    png_image_free(&png);
    return std::nullopt;
  }
  png.format = PNG_FORMAT_RGBA;
  Image image;
  image.width = png.width;
  image.height = png.height;
  // an absurd header must not make us allocate gigabytes
  if (static_cast<uint64_t>(png.width) * png.height > (1ull << 28)) {
    png_image_free(&png);
    return std::nullopt;
  }
  image.rgba.resize(PNG_IMAGE_SIZE(png));
  if (!png_image_finish_read(&png, nullptr, image.rgba.data(), 0, nullptr)) {
    png_image_free(&png);
    return std::nullopt;
  }
  if (!image.IsValid())
    return std::nullopt;
  return image;
}

namespace {
struct JpegError {
  jpeg_error_mgr manager;
  jmp_buf jump;
};

void JpegErrorExit(j_common_ptr info) {
  longjmp(reinterpret_cast<JpegError*>(info->err)->jump, 1);
}

void JpegSilence(j_common_ptr, int) {}

// Composites RGBA over the background into RGB.
void FlattenToRgb(const Image& image, const Rgba8& background, std::vector<uint8_t>* rgb) {
  rgb->resize(static_cast<size_t>(image.width) * image.height * 3);
  const uint32_t backdrop[3] = {background.r, background.g, background.b};
  for (size_t idx = 0, out = 0; idx < image.rgba.size(); idx += 4) {
    const uint32_t alpha = image.rgba[idx + 3];
    for (auto channel = 0; channel < 3; channel++)
      (*rgb)[out++] =
          static_cast<uint8_t>((image.rgba[idx + channel] * alpha + backdrop[channel] * (255 - alpha) + 127) / 255);
  }
}

// Returns false on a libjpeg error. `*out` is malloc'd by libjpeg.
auto CompressJpeg(const uint8_t* rgb, const uint32_t width, const uint32_t height, const int quality,
                  unsigned char** out, unsigned long* out_size) -> bool {
  jpeg_compress_struct info;
  JpegError error;
  info.err = jpeg_std_error(&error.manager);
  error.manager.error_exit = &JpegErrorExit;
  error.manager.emit_message = &JpegSilence;
  if (setjmp(error.jump)) {
    jpeg_destroy_compress(&info);
    std::free(*out);
    *out = nullptr;
    *out_size = 0;
    return false;
  }
  jpeg_create_compress(&info);
  jpeg_mem_dest(&info, out, out_size);
  info.image_width = width;
  info.image_height = height;
  info.input_components = 3;
  info.in_color_space = JCS_RGB;
  jpeg_set_defaults(&info);
  jpeg_set_quality(&info, quality, TRUE);
  jpeg_start_compress(&info, TRUE);
  while (info.next_scanline < info.image_height) {
    JSAMPROW row = const_cast<uint8_t*>(rgb + static_cast<size_t>(info.next_scanline) * width * 3);
    jpeg_write_scanlines(&info, &row, 1);
  }
  jpeg_finish_compress(&info);
  jpeg_destroy_compress(&info);
  return true;
}

// Returns false on a libjpeg error. `rgb` must be big enough for `width` x
// `height` (the caller checked the size first with ReadJpegSize).
auto ReadJpegSize(const uint8_t* data, const size_t size, uint32_t* width, uint32_t* height) -> bool {
  jpeg_decompress_struct info;
  JpegError error;
  info.err = jpeg_std_error(&error.manager);
  error.manager.error_exit = &JpegErrorExit;
  error.manager.emit_message = &JpegSilence;
  if (setjmp(error.jump)) {
    jpeg_destroy_decompress(&info);
    return false;
  }
  jpeg_create_decompress(&info);
  jpeg_mem_src(&info, data, size);
  jpeg_read_header(&info, TRUE);
  *width = info.image_width;
  *height = info.image_height;
  jpeg_destroy_decompress(&info);
  return true;
}

auto DecompressJpeg(const uint8_t* data, const size_t size, uint8_t* rgb, const size_t row_bytes) -> bool {
  jpeg_decompress_struct info;
  JpegError error;
  info.err = jpeg_std_error(&error.manager);
  error.manager.error_exit = &JpegErrorExit;
  error.manager.emit_message = &JpegSilence;
  if (setjmp(error.jump)) {
    jpeg_destroy_decompress(&info);
    return false;
  }
  jpeg_create_decompress(&info);
  jpeg_mem_src(&info, data, size);
  jpeg_read_header(&info, TRUE);
  info.out_color_space = JCS_RGB;
  jpeg_start_decompress(&info);
  while (info.output_scanline < info.output_height) {
    JSAMPROW row = rgb + static_cast<size_t>(info.output_scanline) * row_bytes;
    jpeg_read_scanlines(&info, &row, 1);
  }
  jpeg_finish_decompress(&info);
  jpeg_destroy_decompress(&info);
  return true;
}
}  // namespace

auto EncodeJpeg(const Image& image, const int quality, const Rgba8& background) -> std::optional<std::vector<uint8_t>> {
  if (!image.IsValid())
    return std::nullopt;
  std::vector<uint8_t> rgb;
  FlattenToRgb(image, background, &rgb);
  unsigned char* buffer = nullptr;
  unsigned long size = 0;
  if (!CompressJpeg(rgb.data(), image.width, image.height, std::clamp(quality, 1, 100), &buffer, &size))
    return std::nullopt;
  std::vector<uint8_t> bytes(buffer, buffer + size);
  std::free(buffer);
  return bytes;
}

auto DecodeJpeg(const std::vector<uint8_t>& bytes) -> std::optional<Image> {
  if (bytes.empty())
    return std::nullopt;
  uint32_t width = 0;
  uint32_t height = 0;
  if (!ReadJpegSize(bytes.data(), bytes.size(), &width, &height) || width == 0 || height == 0 ||
      static_cast<uint64_t>(width) * height > (1ull << 28))
    return std::nullopt;
  std::vector<uint8_t> rgb(static_cast<size_t>(width) * height * 3);
  if (!DecompressJpeg(bytes.data(), bytes.size(), rgb.data(), static_cast<size_t>(width) * 3))
    return std::nullopt;
  Image image;
  image.width = width;
  image.height = height;
  image.rgba.resize(static_cast<size_t>(width) * height * 4);
  for (size_t idx = 0, in = 0; idx < image.rgba.size(); idx += 4, in += 3) {
    image.rgba[idx + 0] = rgb[in + 0];
    image.rgba[idx + 1] = rgb[in + 1];
    image.rgba[idx + 2] = rgb[in + 2];
    image.rgba[idx + 3] = 255;
  }
  return image;
}

// ---- files ----------------------------------------------------------------

namespace {
auto WriteFile(const std::string& path, const std::optional<std::vector<uint8_t>>& bytes) -> bool {
  if (!bytes)
    return false;
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
  return static_cast<bool>(out);
}
}  // namespace

auto SavePng(const Image& image, const std::string& path) -> bool {
  return WriteFile(path, EncodePng(image));
}

auto SaveJpeg(const Image& image, const std::string& path, const int quality, const Rgba8& background) -> bool {
  return WriteFile(path, EncodeJpeg(image, quality, background));
}

auto LoadPng(const std::string& path) -> std::optional<Image> {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return std::nullopt;
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return DecodePng(bytes);
}

// ---- comparing ------------------------------------------------------------

auto Compare(const Image& expected, const Image& actual, const uint8_t tolerance) -> ImageDiff {
  ImageDiff diff;
  if (expected.width != actual.width || expected.height != actual.height ||
      expected.rgba.size() != actual.rgba.size()) {
    diff.same_size = false;
    return diff;
  }
  for (size_t idx = 0; idx < expected.rgba.size(); idx += 4) {
    auto worst = 0;
    for (auto channel = 0; channel < 4; channel++)
      worst = std::max(worst, std::abs(static_cast<int>(expected.rgba[idx + channel]) -
                                       static_cast<int>(actual.rgba[idx + channel])));
    diff.max_delta = std::max(diff.max_delta, static_cast<uint8_t>(worst));
    if (worst > tolerance)
      diff.differing_pixels++;
  }
  return diff;
}

auto MakeDiffImage(const Image& expected, const Image& actual, const uint8_t tolerance) -> Image {
  Image out;
  if (!Compare(expected, actual, tolerance).same_size)
    return out;
  out.width = expected.width;
  out.height = expected.height;
  out.rgba.resize(expected.rgba.size());
  for (size_t idx = 0; idx < expected.rgba.size(); idx += 4) {
    auto worst = 0;
    for (auto channel = 0; channel < 4; channel++)
      worst = std::max(worst, std::abs(static_cast<int>(expected.rgba[idx + channel]) -
                                       static_cast<int>(actual.rgba[idx + channel])));
    if (worst > tolerance) {
      out.rgba[idx + 0] = 255;
      out.rgba[idx + 1] = 0;
      out.rgba[idx + 2] = 0;
      out.rgba[idx + 3] = 255;
    } else {
      // the expected pixel at 25%, over white
      for (auto channel = 0; channel < 3; channel++)
        out.rgba[idx + channel] = static_cast<uint8_t>((expected.rgba[idx + channel] * 64 + 255 * 191) / 255);
      out.rgba[idx + 3] = 255;
    }
  }
  return out;
}
}  // namespace kura::renderer
