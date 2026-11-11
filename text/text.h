#ifndef KURA_TEXT_TEXT_H
#define KURA_TEXT_TEXT_H

// Text for the renderers: shaping with HarfBuzz, glyph rasterization with FreeType, and finding the font
// for a `font-family`.
//
// Nothing here draws. A renderer asks a `FontLibrary` for the `Font` of a family, shapes a string into
// glyphs, and decides for itself how to put those glyphs on the screen (the GL renderer packs them into an
// atlas per family, the image renderer blends their bitmaps straight into its pixels). Measuring goes
// through the same shaping, so every renderer that uses this reports the same size for the same text.
//
// A `FontLibrary` and its fonts are not thread safe. A renderer owns one, and the runtime only ever uses a
// renderer from the thread it was set on.

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace kura::text {
// One shaped glyph. Distances are in pixels, and y grows downwards, so a positive `y_offset` moves the
// glyph down, as everywhere else in Kura.
struct Glyph {
  // The glyph's index in its font. It is not a code point.
  uint32_t id = 0;
  // The byte offset in the line's text of the first code point this glyph stands for.
  uint32_t cluster = 0;
  float x_advance = 0;
  float y_advance = 0;
  float x_offset = 0;
  float y_offset = 0;
};

// The glyphs of one line of text (text is split into lines at '\n') in visual order, left to right.
struct Line {
  std::vector<Glyph> glyphs{};
  float width = 0;
};

// What a text takes up. The same text, font and size always measure the same.
struct Metrics {
  // The widest line, and every line.
  float width = 0;
  float height = 0;
  // The distance from the top of the box to the baseline of the first line.
  float ascent = 0;
  size_t lines = 0;
};

struct FontMetrics {
  float ascent = 0;
  float descent = 0;
  // The distance between the baselines of two lines.
  float line_height = 0;
};

// An 8-bit coverage bitmap of one glyph. `left` and `top` place it from the pen position on the baseline:
// the bitmap's top-left corner is at (pen.x + left, baseline - top).
struct GlyphBitmap {
  int width = 0;
  int height = 0;
  int left = 0;
  int top = 0;
  std::vector<uint8_t> alpha{};
};

// One font file at every size. Glyph ids are only meaningful for the font that shaped them.
class Font {
 public:
  ~Font();
  Font(const Font&) = delete;
  Font& operator=(const Font&) = delete;

  // The family the font really is, which is not always the family that was asked for (see
  // `FontLibrary::IsFallback`).
  auto GetFamily() const -> const std::string&;
  auto GetPath() const -> const std::string&;

  auto GetMetrics(float size) -> FontMetrics;

  // Shapes `utf8` at `size` pixels. There is always at least one line, even for empty text.
  auto Shape(std::string_view utf8, float size) -> std::vector<Line>;
  auto Measure(std::string_view utf8, float size) -> Metrics;

  // Draws one glyph at `size` pixels into a coverage bitmap. A glyph with no outline (a space) has an
  // empty bitmap. The pen position is at whole pixels, so the bitmap can be cached per (glyph, size).
  auto Rasterize(uint32_t glyph, float size) -> GlyphBitmap;

 private:
  friend class FontLibrary;
  struct Impl;
  explicit Font(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

class FontLibrary {
 public:
  FontLibrary();
  ~FontLibrary();
  FontLibrary(const FontLibrary&) = delete;
  FontLibrary& operator=(const FontLibrary&) = delete;

  // Makes `family` (case-insensitive) the font file at `path`, ahead of whatever is installed. It takes
  // effect for families not looked up yet.
  void AddFile(std::string_view family, std::string path);

  // The font for a `font-family` value: the first of a comma-separated list that is installed, as in CSS
  // (`"Inter", sans-serif`), and when none is, the system's default font. An empty family is the default.
  // Null only when there isn't a usable font on the system at all. The font lives as long as the library.
  auto Find(std::string_view family) -> Font*;

  // True when `Find(family)` is the system's default font standing in for a family that isn't installed,
  // rather than a font that really is that family. (It's a property of the lookup, not of the font: the
  // default font is also found by its own name.)
  auto IsFallback(std::string_view family) -> bool;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace kura::text

#endif  // KURA_TEXT_TEXT_H
