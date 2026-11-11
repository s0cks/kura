#include "text/text.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb-ft.h>
#include <hb.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <unordered_map>

#ifdef KURA_TEXT_HAS_FONTCONFIG
#include <fontconfig/fontconfig.h>
#endif

namespace kura::text {
namespace {
constexpr float kMinSize = 1.0f;
constexpr float kMaxSize = 1024.0f;

auto Lower(const std::string_view value) -> std::string {
  std::string out(value);
  std::transform(out.begin(), out.end(), out.begin(), [](const unsigned char c) { return std::tolower(c); });
  return out;
}

auto Trim(std::string_view value) -> std::string_view {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
    value.remove_prefix(1);
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
    value.remove_suffix(1);
  return value;
}

// `"Inter", 'Noto Sans', sans-serif` -> Inter, Noto Sans, sans-serif
auto SplitFamilies(const std::string_view list) -> std::vector<std::string> {
  std::vector<std::string> out{};
  size_t pos = 0;
  while (pos <= list.size()) {
    const auto end = std::min(list.find(',', pos), list.size());
    auto name = Trim(list.substr(pos, end - pos));
    if (name.size() >= 2 && (name.front() == '"' || name.front() == '\'') && name.back() == name.front())
      name = Trim(name.substr(1, name.size() - 2));
    if (!name.empty())
      out.emplace_back(name);
    pos = end + 1;
  }
  return out;
}

auto IsGeneric(const std::string& lower) -> bool {
  return lower == "sans-serif" || lower == "serif" || lower == "monospace" || lower == "system-ui" ||
         lower == "ui-sans-serif" || lower == "ui-serif" || lower == "ui-monospace";
}

auto ClampSize(const float size) -> float {
  return std::isfinite(size) ? std::clamp(size, kMinSize, kMaxSize) : 16.0f;
}

// Where a font was found. `matched` is every family name the file goes by.
struct Match {
  std::string path{};
  int index = 0;
  std::vector<std::string> matched{};
};

#ifdef KURA_TEXT_HAS_FONTCONFIG
auto MatchWithFontconfig(const std::string& family, Match* out) -> bool {
  const auto pattern = FcPatternCreate();
  if (pattern == nullptr)
    return false;
  const auto query = family.empty() ? std::string("sans-serif") : family;
  FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8*>(query.c_str()));
  FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
  FcDefaultSubstitute(pattern);

  FcResult result{};
  const auto font = FcFontMatch(nullptr, pattern, &result);
  FcPatternDestroy(pattern);
  if (font == nullptr)
    return false;

  auto ok = false;
  FcChar8* file = nullptr;
  if (FcPatternGetString(font, FC_FILE, 0, &file) == FcResultMatch && file != nullptr) {
    out->path = reinterpret_cast<const char*>(file);
    int index = 0;
    if (FcPatternGetInteger(font, FC_INDEX, 0, &index) == FcResultMatch)
      out->index = index;
    FcChar8* name = nullptr;
    for (int i = 0; FcPatternGetString(font, FC_FAMILY, i, &name) == FcResultMatch; i++)
      out->matched.emplace_back(Lower(reinterpret_cast<const char*>(name)));
    ok = true;
  }
  FcPatternDestroy(font);
  return ok;
}
#endif  // KURA_TEXT_HAS_FONTCONFIG

// When there is no fontconfig, or it has nothing: fonts that are on most systems.
auto MatchWellKnown(Match* out) -> bool {
  static constexpr const char* kPaths[] = {
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/TTF/DejaVuSans.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
      "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
      "/usr/share/fonts/noto/NotoSans-Regular.ttf",
      "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
      "/System/Library/Fonts/Supplemental/Arial.ttf",
      "/System/Library/Fonts/Helvetica.ttc",
      "/Library/Fonts/Arial.ttf",
      "C:/Windows/Fonts/segoeui.ttf",
      "C:/Windows/Fonts/arial.ttf",
  };
  std::error_code ec{};
  for (const auto path : kPaths) {
    if (std::filesystem::exists(path, ec)) {
      out->path = path;
      out->index = 0;
      return true;
    }
  }
  return false;
}
}  // namespace

// ── Font ────────────────────────────────────────────────────────────────────

struct Font::Impl {
  FT_Face face = nullptr;
  hb_font_t* hb = nullptr;
  std::string family{};
  std::string path{};
  // The size the face is set to now, in 26.6 points, so a run of calls at one size doesn't redo it.
  FT_F26Dot6 current = 0;

  ~Impl() {
    if (hb != nullptr)
      hb_font_destroy(hb);
    if (face != nullptr)
      FT_Done_Face(face);
  }

  // 1 point is 1 pixel at 72 dpi, so a size in points is a size in pixels.
  void SetSize(const float size) {
    const auto wanted = static_cast<FT_F26Dot6>(std::lround(ClampSize(size) * 64.0f));
    if (wanted == current)
      return;
    if (FT_Set_Char_Size(face, 0, wanted, 72, 72) != 0) {
      // a bitmap-only font has fixed sizes, and the closest will have to do
      if (face->num_fixed_sizes > 0)
        FT_Select_Size(face, 0);
    }
    hb_ft_font_changed(hb);
    current = wanted;
  }
};

Font::Font(std::unique_ptr<Impl> impl) :
  impl_(std::move(impl)) {}

Font::~Font() = default;

auto Font::GetFamily() const -> const std::string& {
  return impl_->family;
}

auto Font::GetPath() const -> const std::string& {
  return impl_->path;
}

auto Font::GetMetrics(const float size) -> FontMetrics {
  impl_->SetSize(size);
  const auto& m = impl_->face->size->metrics;
  FontMetrics out{};
  out.ascent = static_cast<float>(m.ascender) / 64.0f;
  out.descent = -static_cast<float>(m.descender) / 64.0f;
  out.line_height = static_cast<float>(m.height) / 64.0f;
  if (out.line_height <= 0.0f)
    out.line_height = out.ascent + out.descent;
  return out;
}

auto Font::Shape(const std::string_view utf8, const float size) -> std::vector<Line> {
  impl_->SetSize(size);
  std::vector<Line> lines{};
  size_t pos = 0;
  while (true) {
    const auto end = std::min(utf8.find('\n', pos), utf8.size());
    auto text = utf8.substr(pos, end - pos);
    if (!text.empty() && text.back() == '\r')
      text.remove_suffix(1);

    Line line{};
    if (!text.empty()) {
      const auto buffer = hb_buffer_create();
      hb_buffer_add_utf8(buffer, text.data(), static_cast<int>(text.size()), 0, static_cast<int>(text.size()));
      hb_buffer_guess_segment_properties(buffer);
      hb_shape(impl_->hb, buffer, nullptr, 0);

      unsigned int count = 0;
      const auto info = hb_buffer_get_glyph_infos(buffer, &count);
      const auto position = hb_buffer_get_glyph_positions(buffer, &count);
      line.glyphs.reserve(count);
      for (unsigned int i = 0; i < count; i++) {
        Glyph glyph{};
        glyph.id = info[i].codepoint;
        glyph.cluster = info[i].cluster;
        glyph.x_advance = static_cast<float>(position[i].x_advance) / 64.0f;
        glyph.y_advance = -static_cast<float>(position[i].y_advance) / 64.0f;
        glyph.x_offset = static_cast<float>(position[i].x_offset) / 64.0f;
        glyph.y_offset = -static_cast<float>(position[i].y_offset) / 64.0f;
        line.width += glyph.x_advance;
        line.glyphs.push_back(glyph);
      }
      hb_buffer_destroy(buffer);
    }
    lines.push_back(std::move(line));
    if (end >= utf8.size())
      break;
    pos = end + 1;
  }
  return lines;
}

auto Font::Measure(const std::string_view utf8, const float size) -> Metrics {
  const auto lines = Shape(utf8, size);
  const auto font = GetMetrics(size);
  Metrics out{};
  out.lines = lines.size();
  out.ascent = font.ascent;
  out.height = font.line_height * static_cast<float>(lines.size());
  for (const auto& line : lines)
    out.width = std::max(out.width, line.width);
  return out;
}

auto Font::Rasterize(const uint32_t glyph, const float size) -> GlyphBitmap {
  impl_->SetSize(size);
  GlyphBitmap out{};
  if (FT_Load_Glyph(impl_->face, glyph, FT_LOAD_RENDER | FT_LOAD_NO_HINTING) != 0)
    return out;
  const auto& slot = *impl_->face->glyph;
  const auto& bitmap = slot.bitmap;
  if (bitmap.width == 0 || bitmap.rows == 0 || bitmap.buffer == nullptr)
    return out;

  out.width = static_cast<int>(bitmap.width);
  out.height = static_cast<int>(bitmap.rows);
  out.left = slot.bitmap_left;
  out.top = slot.bitmap_top;
  out.alpha.resize(static_cast<size_t>(out.width) * static_cast<size_t>(out.height));
  const auto pitch = std::abs(bitmap.pitch);
  for (int y = 0; y < out.height; y++) {
    // a negative pitch is a bitmap stored bottom-up
    const auto row = bitmap.pitch >= 0 ? y : out.height - 1 - y;
    const auto source = bitmap.buffer + static_cast<size_t>(row) * static_cast<size_t>(pitch);
    auto target = out.alpha.data() + static_cast<size_t>(y) * static_cast<size_t>(out.width);
    if (bitmap.pixel_mode == FT_PIXEL_MODE_MONO) {
      for (int x = 0; x < out.width; x++)
        target[x] = (source[x / 8] & (0x80 >> (x % 8))) != 0 ? 255 : 0;
    } else {
      std::copy(source, source + out.width, target);
    }
  }
  return out;
}

// ── FontLibrary ─────────────────────────────────────────────────────────────

struct FontLibrary::Impl {
  FT_Library ft = nullptr;
  // a file that is a family, ahead of what's installed (lowercase family -> path)
  std::unordered_map<std::string, std::string> files{};
  // every font opened, by file and face
  std::unordered_map<std::string, std::unique_ptr<Font>> fonts{};
  // what a family value resolved to, including to nothing, so a missing family is only searched for once
  struct Resolution {
    Font* font = nullptr;
    bool fallback = false;
  };
  std::unordered_map<std::string, Resolution> resolved{};

  Impl() {
    if (FT_Init_FreeType(&ft) != 0)
      ft = nullptr;
  }

  ~Impl() {
    // the fonts hold faces that belong to the library, so they go first
    resolved.clear();
    fonts.clear();
    if (ft != nullptr)
      FT_Done_FreeType(ft);
  }

  auto Open(const Match& match, const std::string& family) -> Font* {
    if (ft == nullptr)
      return nullptr;
    const auto key = match.path + "#" + std::to_string(match.index);
    if (const auto it = fonts.find(key); it != fonts.end()) {
      // the same file asked for by a name it's really called, and then as a stand-in, is the same font
      return it->second.get();
    }

    auto impl = std::make_unique<Font::Impl>();
    if (FT_New_Face(ft, match.path.c_str(), match.index, &impl->face) != 0) {
      impl->face = nullptr;
      return nullptr;
    }
    if (FT_Select_Charmap(impl->face, FT_ENCODING_UNICODE) != 0 && impl->face->num_charmaps > 0)
      FT_Set_Charmap(impl->face, impl->face->charmaps[0]);
    impl->hb = hb_ft_font_create_referenced(impl->face);
    if (impl->hb == nullptr)
      return nullptr;
    hb_ft_font_set_load_flags(impl->hb, FT_LOAD_NO_HINTING);
    impl->path = match.path;
    impl->family = impl->face->family_name != nullptr ? impl->face->family_name : family;
    impl->SetSize(16.0f);

    auto font = std::unique_ptr<Font>(new Font(std::move(impl)));
    const auto raw = font.get();
    fonts[key] = std::move(font);
    return raw;
  }

  // One family name: the registered file, then what's installed under that name. Null if it isn't.
  auto FindExact(const std::string& name) -> Font* {
    const auto lower = Lower(name);
    if (const auto it = files.find(lower); it != files.end()) {
      Match match{};
      match.path = it->second;
      if (const auto font = Open(match, name))
        return font;
    }
#ifdef KURA_TEXT_HAS_FONTCONFIG
    Match match{};
    if (MatchWithFontconfig(name, &match)) {
      // fontconfig always answers, with the closest it has, so a family only counts as found when it's one
      // of the names the file goes by
      const auto found = IsGeneric(lower) || std::find(match.matched.begin(), match.matched.end(), lower) !=
                                                 match.matched.end();
      if (found)
        return Open(match, name);
    }
#endif
    return nullptr;
  }

  // The font to fall back to: the system's default, or failing that a font that's on most systems.
  auto FindDefault() -> Font* {
#ifdef KURA_TEXT_HAS_FONTCONFIG
    Match match{};
    if (MatchWithFontconfig("", &match)) {
      if (const auto font = Open(match, ""))
        return font;
    }
#endif
    Match well_known{};
    if (MatchWellKnown(&well_known))
      return Open(well_known, "");
    return nullptr;
  }
};

FontLibrary::FontLibrary() :
  impl_(std::make_unique<Impl>()) {}

FontLibrary::~FontLibrary() = default;

void FontLibrary::AddFile(const std::string_view family, std::string path) {
  impl_->files[Lower(Trim(family))] = std::move(path);
}

auto FontLibrary::Find(const std::string_view family) -> Font* {
  const auto key = Lower(Trim(family));
  if (const auto it = impl_->resolved.find(key); it != impl_->resolved.end())
    return it->second.font;

  Font* font = nullptr;
  for (const auto& name : SplitFamilies(family)) {
    if ((font = impl_->FindExact(name)) != nullptr)
      break;
  }
  // no family asked for is the default itself, not a stand-in for one
  const auto asked = !SplitFamilies(family).empty();
  auto fallback = false;
  if (font == nullptr) {
    font = impl_->FindDefault();
    fallback = asked && font != nullptr;
  }
  impl_->resolved[key] = {font, fallback};
  return font;
}

auto FontLibrary::IsFallback(const std::string_view family) -> bool {
  Find(family);
  return impl_->resolved[Lower(Trim(family))].fallback;
}
}  // namespace kura::text
