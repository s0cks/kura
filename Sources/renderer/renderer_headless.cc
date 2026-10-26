#include "renderer/renderer_headless.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace kura::renderer {
namespace {
using Status = KuraRendererStatus;

// Coordinates are clamped to this before they become ints, so that a rect at
// 1e30 can't overflow anything. It's far larger than any surface.
constexpr double kCoordLimit = 1 << 20;

inline auto ToInt(const double value) -> int {
  return static_cast<int>(std::clamp(value, -kCoordLimit, kCoordLimit));
}

inline auto IsValid(const float value) -> bool {
  return std::isfinite(value);
}

inline auto IsValid(const KuraVec2& value) -> bool {
  return IsValid(value.x) && IsValid(value.y);
}

inline auto IsValid(const KuraRect& value) -> bool {
  return IsValid(value.x) && IsValid(value.y) && IsValid(value.width) && IsValid(value.height) &&
         value.width >= 0.0f && value.height >= 0.0f;
}

inline auto IsValid(const KuraColor& value) -> bool {
  return IsValid(value.r) && IsValid(value.g) && IsValid(value.b) && IsValid(value.a);
}

inline auto IsValid(const KuraTextStyle& value) -> bool {
  return IsValid(value.size) && value.size > 0.0f;
}

inline auto Quantize(const float channel) -> uint8_t {
  const auto clamped = std::min(1.0f, std::max(0.0f, channel));
  return static_cast<uint8_t>(std::lround(static_cast<double>(clamped) * 255.0));
}

inline auto ToRgba8(const KuraColor& color) -> Rgba8 {
  return Rgba8{Quantize(color.r), Quantize(color.g), Quantize(color.b), Quantize(color.a)};
}

// A half-open rect of pixels: [x0, x1) x [y0, y1).
struct PixelRect {
  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;

  inline auto IsEmpty() const -> bool {
    return x1 <= x0 || y1 <= y0;
  }

  inline auto Contains(const int x, const int y) const -> bool {
    return x >= x0 && x < x1 && y >= y0 && y < y1;
  }

  inline auto Intersect(const PixelRect& rhs) const -> PixelRect {
    const PixelRect result{std::max(x0, rhs.x0), std::max(y0, rhs.y0), std::min(x1, rhs.x1), std::min(y1, rhs.y1)};
    // every empty rect is the same rect
    return result.IsEmpty() ? PixelRect{} : result;
  }
};

// The pixels whose centers are inside the rect.
inline auto CoveredPixels(const KuraRect& rect) -> PixelRect {
  const auto x = static_cast<double>(rect.x);
  const auto y = static_cast<double>(rect.y);
  return PixelRect{
      ToInt(std::ceil(x - 0.5)),
      ToInt(std::ceil(y - 0.5)),
      ToInt(std::ceil(x + static_cast<double>(rect.width) - 0.5)),
      ToInt(std::ceil(y + static_cast<double>(rect.height) - 0.5)),
  };
}

inline void AppendNumber(std::string& out, float value) {
  value += 0.0f;  // -0 -> 0
  char buffer[32];
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
  out.append(buffer, result.ptr);
}

inline void AppendColor(std::string& out, const KuraColor& color) {
  const auto rgba = ToRgba8(color);
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x%02x", rgba.r, rgba.g, rgba.b, rgba.a);
  out.append(buffer);
}

inline void AppendQuoted(std::string& out, const std::string& text) {
  out.push_back('"');
  for (const char c : text) {
    const auto byte = static_cast<unsigned char>(c);
    if (c == '"' || c == '\\') {
      out.push_back('\\');
      out.push_back(c);
    } else if (c == '\n') {
      out.append("\\n");
    } else if (byte < 0x20 || byte == 0x7f) {
      char buffer[8];
      std::snprintf(buffer, sizeof(buffer), "\\x%02x", byte);
      out.append(buffer);
    } else {
      out.push_back(c);
    }
  }
  out.push_back('"');
}

inline auto SameFloat(const float a, const float b) -> bool {
  return a == b;
}

inline auto Same(const KuraRect& a, const KuraRect& b) -> bool {
  return SameFloat(a.x, b.x) && SameFloat(a.y, b.y) && SameFloat(a.width, b.width) && SameFloat(a.height, b.height);
}

inline auto Same(const KuraVec2& a, const KuraVec2& b) -> bool {
  return SameFloat(a.x, b.x) && SameFloat(a.y, b.y);
}

inline auto Same(const KuraColor& a, const KuraColor& b) -> bool {
  return SameFloat(a.r, b.r) && SameFloat(a.g, b.g) && SameFloat(a.b, b.b) && SameFloat(a.a, b.a);
}
}  // namespace

auto Command::operator==(const Command& rhs) const -> bool {
  return kind == rhs.kind && Same(rect, rhs.rect) && Same(from, rhs.from) && Same(to, rhs.to) &&
         Same(color, rhs.color) && text == rhs.text && SameFloat(font_size, rhs.font_size);
}

class HeadlessImpl {
 public:
  explicit HeadlessImpl(const HeadlessOptions& options) :
    options_(options),
    clear_(ToRgba8(options.clear)) {}

  auto BeginFrame(const uint32_t width, const uint32_t height) -> Status {
    if (in_frame_)
      return KURA_RENDERER_ERR_IN_FRAME;
    if (width > KURA_RENDERER_MAX_SURFACE_SIZE || height > KURA_RENDERER_MAX_SURFACE_SIZE)
      return KURA_RENDERER_ERR_INVALID_ARG;

    pixels_.clear();
    if (options_.rasterize) {
      pixels_.resize(static_cast<size_t>(width) * height * 4);
      for (size_t idx = 0; idx < pixels_.size(); idx += 4) {
        pixels_[idx + 0] = clear_.r;
        pixels_[idx + 1] = clear_.g;
        pixels_[idx + 2] = clear_.b;
        pixels_[idx + 3] = clear_.a;
      }
    }
    width_ = width;
    height_ = height;
    commands_.clear();
    clips_.clear();
    in_frame_ = true;
    return KURA_RENDERER_OK;
  }

  auto EndFrame() -> Status {
    if (!in_frame_)
      return KURA_RENDERER_ERR_NOT_IN_FRAME;
    in_frame_ = false;
    frame_count_++;
    const auto balanced = clips_.empty();
    clips_.clear();
    return balanced ? KURA_RENDERER_OK : KURA_RENDERER_ERR_CLIP_UNBALANCED;
  }

  auto PushClip(const KuraRect& rect) -> Status {
    if (const auto status = Check(rect); status != KURA_RENDERER_OK)
      return status;
    clips_.push_back(CurrentClip().Intersect(CoveredPixels(rect)));
    Command command{Command::Kind::kPushClip};
    command.rect = rect;
    commands_.push_back(std::move(command));
    return KURA_RENDERER_OK;
  }

  auto PopClip() -> Status {
    if (!in_frame_)
      return KURA_RENDERER_ERR_NOT_IN_FRAME;
    if (clips_.empty())
      return KURA_RENDERER_ERR_CLIP_UNDERFLOW;
    clips_.pop_back();
    commands_.push_back(Command{Command::Kind::kPopClip});
    return KURA_RENDERER_OK;
  }

  auto FillRect(const KuraRect& rect, const KuraColor& color) -> Status {
    if (const auto status = Check(rect, color); status != KURA_RENDERER_OK)
      return status;
    Command command{Command::Kind::kFillRect};
    command.rect = rect;
    command.color = color;
    commands_.push_back(std::move(command));

    if (options_.rasterize)
      FillPixels(CoveredPixels(rect).Intersect(CurrentClip()), ToRgba8(color));
    return KURA_RENDERER_OK;
  }

  auto StrokeRect(const KuraRect& rect, const KuraColor& color) -> Status {
    if (const auto status = Check(rect, color); status != KURA_RENDERER_OK)
      return status;
    Command command{Command::Kind::kStrokeRect};
    command.rect = rect;
    command.color = color;
    commands_.push_back(std::move(command));

    if (!options_.rasterize)
      return KURA_RENDERER_OK;
    const auto box = CoveredPixels(rect);
    if (box.IsEmpty())
      return KURA_RENDERER_OK;
    // the ring, with every pixel in it drawn once
    const auto clip = CurrentClip();
    const auto rgba = ToRgba8(color);
    FillPixels(PixelRect{box.x0, box.y0, box.x1, box.y0 + 1}.Intersect(clip), rgba);
    if (box.y1 - 1 != box.y0)
      FillPixels(PixelRect{box.x0, box.y1 - 1, box.x1, box.y1}.Intersect(clip), rgba);
    FillPixels(PixelRect{box.x0, box.y0 + 1, box.x0 + 1, box.y1 - 1}.Intersect(clip), rgba);
    if (box.x1 - 1 != box.x0)
      FillPixels(PixelRect{box.x1 - 1, box.y0 + 1, box.x1, box.y1 - 1}.Intersect(clip), rgba);
    return KURA_RENDERER_OK;
  }

  auto DrawLine(const KuraVec2& from, const KuraVec2& to, const KuraColor& color) -> Status {
    if (!in_frame_)
      return KURA_RENDERER_ERR_NOT_IN_FRAME;
    if (!IsValid(from) || !IsValid(to) || !IsValid(color))
      return KURA_RENDERER_ERR_INVALID_ARG;
    Command command{Command::Kind::kLine};
    command.from = from;
    command.to = to;
    command.color = color;
    commands_.push_back(std::move(command));

    if (!options_.rasterize)
      return KURA_RENDERER_OK;
    const auto rgba = ToRgba8(color);
    const auto clip = CurrentClip();
    int64_t x = ToInt(std::floor(static_cast<double>(from.x)));
    int64_t y = ToInt(std::floor(static_cast<double>(from.y)));
    const int64_t x1 = ToInt(std::floor(static_cast<double>(to.x)));
    const int64_t y1 = ToInt(std::floor(static_cast<double>(to.y)));
    const int64_t dx = std::abs(x1 - x);
    const int64_t dy = -std::abs(y1 - y);
    const int64_t step_x = x < x1 ? 1 : -1;
    const int64_t step_y = y < y1 ? 1 : -1;
    int64_t error = dx + dy;
    while (true) {
      if (clip.Contains(static_cast<int>(x), static_cast<int>(y)))
        Blend(static_cast<int>(x), static_cast<int>(y), rgba);
      if (x == x1 && y == y1)
        break;
      const auto doubled = 2 * error;
      if (doubled >= dy) {
        error += dy;
        x += step_x;
      }
      if (doubled <= dx) {
        error += dx;
        y += step_y;
      }
    }
    return KURA_RENDERER_OK;
  }

  auto DrawText(const KuraVec2& pos, const char* utf8, const size_t length, const KuraTextStyle& style,
                const KuraColor& color) -> Status {
    if (!in_frame_)
      return KURA_RENDERER_ERR_NOT_IN_FRAME;
    if (!IsValid(pos) || !IsValid(style) || !IsValid(color) || (utf8 == nullptr && length != 0))
      return KURA_RENDERER_ERR_INVALID_ARG;
    Command command{Command::Kind::kText};
    command.from = pos;
    command.color = color;
    command.text.assign(length == 0 ? "" : utf8, length);
    command.font_size = style.size;
    commands_.push_back(std::move(command));
    return KURA_RENDERER_OK;
  }

  static auto MeasureText(const char* utf8, const size_t length, const KuraTextStyle& style, KuraTextMetrics* out)
      -> Status {
    if (out == nullptr || !IsValid(style) || (utf8 == nullptr && length != 0))
      return KURA_RENDERER_ERR_INVALID_ARG;
    size_t lines = 1;
    size_t current = 0;
    size_t widest = 0;
    for (size_t idx = 0; idx < length; idx++) {
      const auto byte = static_cast<unsigned char>(utf8[idx]);
      if (byte == '\n') {
        widest = std::max(widest, current);
        current = 0;
        lines++;
      } else if ((byte & 0xC0) != 0x80) {  // not a continuation byte: a new code point
        current++;
      }
    }
    widest = std::max(widest, current);
    const auto glyph = style.size * 0.5f;
    const auto line = style.size * 1.25f;
    out->width = static_cast<float>(widest) * glyph;
    out->height = static_cast<float>(lines) * line;
    out->ascent = style.size;
    return KURA_RENDERER_OK;
  }

  inline auto InFrame() const -> bool {
    return in_frame_;
  }

  inline auto GetWidth() const -> uint32_t {
    return width_;
  }

  inline auto GetHeight() const -> uint32_t {
    return height_;
  }

  inline auto GetFrameCount() const -> uint64_t {
    return frame_count_;
  }

  inline auto GetClipDepth() const -> size_t {
    return clips_.size();
  }

  inline auto GetCommands() const -> const std::vector<Command>& {
    return commands_;
  }

  inline auto GetPixels() const -> const std::vector<uint8_t>& {
    return pixels_;
  }

 private:
  HeadlessOptions options_;
  Rgba8 clear_;
  bool in_frame_ = false;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  uint64_t frame_count_ = 0;
  // Each entry is the clip in effect after that push: already intersected with
  // every clip below it.
  std::vector<PixelRect> clips_{};
  std::vector<Command> commands_{};
  std::vector<uint8_t> pixels_{};

  inline auto Check(const KuraRect& rect) const -> Status {
    if (!in_frame_)
      return KURA_RENDERER_ERR_NOT_IN_FRAME;
    return IsValid(rect) ? KURA_RENDERER_OK : KURA_RENDERER_ERR_INVALID_ARG;
  }

  inline auto Check(const KuraRect& rect, const KuraColor& color) const -> Status {
    if (const auto status = Check(rect); status != KURA_RENDERER_OK)
      return status;
    return IsValid(color) ? KURA_RENDERER_OK : KURA_RENDERER_ERR_INVALID_ARG;
  }

  inline auto CurrentClip() const -> PixelRect {
    return clips_.empty() ? PixelRect{0, 0, static_cast<int>(width_), static_cast<int>(height_)} : clips_.back();
  }

  void FillPixels(const PixelRect& area, const Rgba8& color) {
    if (area.IsEmpty())
      return;
    for (auto y = area.y0; y < area.y1; y++) {
      for (auto x = area.x0; x < area.x1; x++)
        Blend(x, y, color);
    }
  }

  // Source-over. The caller has already clipped (x, y) to the surface.
  void Blend(const int x, const int y, const Rgba8& src) {
    if (src.a == 0)
      return;
    auto* dst = &pixels_[(static_cast<size_t>(y) * width_ + static_cast<size_t>(x)) * 4];
    if (src.a == 255) {
      dst[0] = src.r;
      dst[1] = src.g;
      dst[2] = src.b;
      dst[3] = 255;
      return;
    }
    const uint32_t sa = src.a;
    const uint32_t da = dst[3];
    const uint32_t inverse = 255 - sa;
    // the result's alpha, times 255
    const uint32_t alpha = sa * 255 + da * inverse;
    const uint32_t channels[3] = {src.r, src.g, src.b};
    for (auto idx = 0; idx < 3; idx++) {
      const uint32_t numerator = channels[idx] * sa * 255 + dst[idx] * da * inverse;
      dst[idx] = static_cast<uint8_t>((numerator + alpha / 2) / alpha);
    }
    dst[3] = static_cast<uint8_t>((alpha + 127) / 255);
  }
};

namespace {
// Nothing thrown may reach the host: it may not be C++, and the ABI has a
// status code for the one thing that can go wrong.
template <typename F>
inline auto Guard(F&& body) -> Status {
  try {
    return body();
  } catch (...) {
    return KURA_RENDERER_ERR_OUT_OF_RESOURCES;
  }
}

inline auto Self(void* self) -> HeadlessImpl* {
  return static_cast<HeadlessImpl*>(self);
}

auto OpBeginFrame(void* self, const uint32_t width, const uint32_t height) -> Status {
  return Guard([&] { return Self(self)->BeginFrame(width, height); });
}

auto OpEndFrame(void* self) -> Status {
  return Guard([&] { return Self(self)->EndFrame(); });
}

auto OpPushClip(void* self, const KuraRect rect) -> Status {
  return Guard([&] { return Self(self)->PushClip(rect); });
}

auto OpPopClip(void* self) -> Status {
  return Guard([&] { return Self(self)->PopClip(); });
}

auto OpFillRect(void* self, const KuraRect rect, const KuraColor color) -> Status {
  return Guard([&] { return Self(self)->FillRect(rect, color); });
}

auto OpStrokeRect(void* self, const KuraRect rect, const KuraColor color) -> Status {
  return Guard([&] { return Self(self)->StrokeRect(rect, color); });
}

auto OpDrawLine(void* self, const KuraVec2 from, const KuraVec2 to, const KuraColor color) -> Status {
  return Guard([&] { return Self(self)->DrawLine(from, to, color); });
}

auto OpDrawText(void* self, const KuraVec2 pos, const char* utf8, const size_t length, const KuraTextStyle style,
                const KuraColor color) -> Status {
  return Guard([&] { return Self(self)->DrawText(pos, utf8, length, style, color); });
}

auto OpMeasureText(void*, const char* utf8, const size_t length, const KuraTextStyle style, KuraTextMetrics* out)
    -> Status {
  return Guard([&] { return HeadlessImpl::MeasureText(utf8, length, style, out); });
}

void OpDestroyOwned(void* self) {
  delete Self(self);
}

void OpDestroyBorrowed(void*) {}

constexpr auto MakeOps(void (*destroy)(void*)) -> KuraRendererOps {
  KuraRendererOps ops{};
  ops.struct_size = sizeof(KuraRendererOps);
  ops.abi_version = KURA_RENDERER_ABI_VERSION;
  ops.destroy = destroy;
  ops.begin_frame = &OpBeginFrame;
  ops.end_frame = &OpEndFrame;
  ops.push_clip = &OpPushClip;
  ops.pop_clip = &OpPopClip;
  ops.fill_rect = &OpFillRect;
  ops.stroke_rect = &OpStrokeRect;
  ops.draw_line = &OpDrawLine;
  ops.draw_text = &OpDrawText;
  ops.measure_text = &OpMeasureText;
  return ops;
}

constexpr KuraRendererOps kOwnedOps = MakeOps(&OpDestroyOwned);
constexpr KuraRendererOps kBorrowedOps = MakeOps(&OpDestroyBorrowed);
}  // namespace

HeadlessRenderer::HeadlessRenderer(const HeadlessOptions& options) :
  impl_(std::make_unique<HeadlessImpl>(options)) {}

HeadlessRenderer::~HeadlessRenderer() = default;

auto HeadlessRenderer::Abi() -> KuraRenderer {
  return KuraRenderer{&kBorrowedOps, impl_.get()};
}

auto HeadlessRenderer::InFrame() const -> bool {
  return impl_->InFrame();
}

auto HeadlessRenderer::GetWidth() const -> uint32_t {
  return impl_->GetWidth();
}

auto HeadlessRenderer::GetHeight() const -> uint32_t {
  return impl_->GetHeight();
}

auto HeadlessRenderer::GetFrameCount() const -> uint64_t {
  return impl_->GetFrameCount();
}

auto HeadlessRenderer::GetClipDepth() const -> size_t {
  return impl_->GetClipDepth();
}

auto HeadlessRenderer::GetCommands() const -> const std::vector<Command>& {
  return impl_->GetCommands();
}

auto HeadlessRenderer::DumpCommands() const -> std::string {
  std::string out = "frame " + std::to_string(GetWidth()) + "x" + std::to_string(GetHeight()) + "\n";
  const auto rect = [&out](const KuraRect& value) {
    for (const auto number : {value.x, value.y, value.width, value.height}) {
      out.push_back(' ');
      AppendNumber(out, number);
    }
  };
  const auto color = [&out](const KuraColor& value) {
    out.push_back(' ');
    AppendColor(out, value);
  };
  for (const auto& command : GetCommands()) {
    switch (command.kind) {
      case Command::Kind::kFillRect:
        out.append("fill-rect");
        rect(command.rect);
        color(command.color);
        break;
      case Command::Kind::kStrokeRect:
        out.append("stroke-rect");
        rect(command.rect);
        color(command.color);
        break;
      case Command::Kind::kLine:
        out.append("line");
        for (const auto number : {command.from.x, command.from.y, command.to.x, command.to.y}) {
          out.push_back(' ');
          AppendNumber(out, number);
        }
        color(command.color);
        break;
      case Command::Kind::kText:
        out.append("text ");
        AppendNumber(out, command.from.x);
        out.push_back(' ');
        AppendNumber(out, command.from.y);
        out.push_back(' ');
        AppendNumber(out, command.font_size);
        color(command.color);
        out.push_back(' ');
        AppendQuoted(out, command.text);
        break;
      case Command::Kind::kPushClip:
        out.append("push-clip");
        rect(command.rect);
        break;
      case Command::Kind::kPopClip:
        out.append("pop-clip");
        break;
    }
    out.push_back('\n');
  }
  return out;
}

auto HeadlessRenderer::GetPixels() const -> const std::vector<uint8_t>& {
  return impl_->GetPixels();
}

auto HeadlessRenderer::PixelAt(const uint32_t x, const uint32_t y) const -> Rgba8 {
  if (x >= GetWidth() || y >= GetHeight() || GetPixels().empty())
    return Rgba8{};
  const auto* pixel = &GetPixels()[(static_cast<size_t>(y) * GetWidth() + x) * 4];
  return Rgba8{pixel[0], pixel[1], pixel[2], pixel[3]};
}

auto HeadlessRenderer::CountPixels(const Rgba8& color) const -> uint64_t {
  uint64_t count = 0;
  const auto& pixels = GetPixels();
  for (size_t idx = 0; idx < pixels.size(); idx += 4) {
    if (pixels[idx] == color.r && pixels[idx + 1] == color.g && pixels[idx + 2] == color.b &&
        pixels[idx + 3] == color.a)
      count++;
  }
  return count;
}

auto HeadlessRenderer::ToPpm(const Rgba8& background) const -> std::string {
  const auto& pixels = GetPixels();
  if (pixels.empty())
    return {};
  std::string out = "P6\n" + std::to_string(GetWidth()) + " " + std::to_string(GetHeight()) + "\n255\n";
  out.reserve(out.size() + pixels.size() / 4 * 3);
  const uint32_t backdrop[3] = {background.r, background.g, background.b};
  for (size_t idx = 0; idx < pixels.size(); idx += 4) {
    const uint32_t alpha = pixels[idx + 3];
    for (auto channel = 0; channel < 3; channel++) {
      const uint32_t value = pixels[idx + channel] * alpha + backdrop[channel] * (255 - alpha);
      out.push_back(static_cast<char>((value + 127) / 255));
    }
  }
  return out;
}

auto HeadlessRenderer::MeasureText(const std::string& text, const float font_size) -> KuraTextMetrics {
  KuraTextMetrics metrics{};
  if (HeadlessImpl::MeasureText(text.data(), text.size(), KuraTextStyle{font_size}, &metrics) != KURA_RENDERER_OK)
    return KuraTextMetrics{};
  return metrics;
}
}  // namespace kura::renderer

extern "C" {
KuraRendererStatus kura_headless_renderer_create(const KuraHeadlessOptions* options, KuraRenderer* renderer) {
  using namespace kura::renderer;
  if (renderer == nullptr)
    return KURA_RENDERER_ERR_INVALID_ARG;
  HeadlessOptions headless{};
  if (options != nullptr) {
    headless.rasterize = options->rasterize != 0;
    headless.clear = options->clear;
  }
  try {
    *renderer = KuraRenderer{&kOwnedOps, new HeadlessImpl(headless)};
  } catch (...) {
    return KURA_RENDERER_ERR_OUT_OF_RESOURCES;
  }
  return KURA_RENDERER_OK;
}
}
