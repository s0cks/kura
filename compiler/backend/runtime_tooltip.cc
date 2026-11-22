// Tooltips. A node with a `tooltip` registers its box and the text as a place of kind `kEventTooltip` as it is drawn
// (see DomLowering::EmitTooltip). When a frame ends, the runtime looks for the topmost of those places that the pointer
// is over, and once the pointer has been over it for `delay` seconds draws its text next to the pointer, over
// everything that was drawn.
//
// The topmost is the one that was drawn last, so what is inside another node has its own tooltip, and what has none
// has the tooltip of the node it is in. The place is cut to the clips it was drawn in, so what is scrolled out of a
// list has none to show where it isn't.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>

#include "backend/runtime_abi.h"
#include "backend/runtime_input.h"
#include "backend/runtime_text.h"
#include "object.h"

namespace {
using kura::rt::KuraValue;

// What tooltips look like when the host doesn't say.
constexpr KuraTooltipStyle kDefaultStyle{
    .delay = 0.5,
    .font_size = 14,
    .max_width = 320,
    .padding_x = 8,
    .padding_y = 5,
    .radius = 4,
    .gap = 8,
    .margin = 4,
    .background = KuraColor{0x22 / 255.0f, 0x22 / 255.0f, 0x22 / 255.0f, 0xF2 / 255.0f},
    .color = KuraColor{0xF5 / 255.0f, 0xF5 / 255.0f, 0xF5 / 255.0f, 1.0f},
};

struct State {
  KuraTooltipStyle style = kDefaultStyle;

  bool has_pointer = false;
  double pointer_x = 0;
  double pointer_y = 0;

  bool clock_set = false;
  double clock = 0;

  // what the pointer was over when the last frame ended: the node's box and its tooltip's text
  bool hovering = false;
  KuraRect box{};
  std::string text{};
  double since = 0;

  // what a click, or the wheel, hid: it stays hidden until the pointer is over something else
  bool dismissed = false;
  KuraRect dismissed_box{};
  std::string dismissed_text{};

  bool pending = false;
};

thread_local State tl_state{};

auto Now() -> double {
  if (tl_state.clock_set)
    return tl_state.clock;
  using Clock = std::chrono::steady_clock;
  return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

auto Bits(const double value) -> uint64_t {
  uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

auto Same(const KuraRect& a, const std::string& a_text, const KuraRect& b, const std::string& b_text) -> bool {
  return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height && a_text == b_text;
}
}  // namespace

extern "C" {
void kura_rt_pointer_moved(const double x, const double y) {
  tl_state.has_pointer = true;
  tl_state.pointer_x = x;
  tl_state.pointer_y = y;
}

void kura_rt_pointer_left() {
  tl_state.has_pointer = false;
}

void kura_rt_set_time(const double seconds) {
  kura::rt::input::SetClock(seconds);
  tl_state.clock_set = true;
  tl_state.clock = seconds;
}

void kura_rt_tooltip_dismiss() {
  auto& state = tl_state;
  if (!state.hovering)
    return;
  state.dismissed = true;
  state.dismissed_box = state.box;
  state.dismissed_text = state.text;
  state.pending = false;
}

int kura_rt_tooltip_pending() {
  return tl_state.pending ? 1 : 0;
}

void kura_rt_tooltip_configure(const KuraTooltipStyle* style) {
  tl_state.style = *style;
}

KuraTooltipStyle kura_rt_tooltip_style() {
  return tl_state.style;
}

KuraTooltipStyle kura_rt_tooltip_default_style() {
  return kDefaultStyle;
}

void kura_rt_tooltip_reset() {
  // what was over, waited for or hidden belonged to the frames of a program that is gone; the host's style and
  // where the pointer is are the host's
  auto& state = tl_state;
  state.hovering = false;
  state.dismissed = false;
  state.pending = false;
  state.text.clear();
  state.dismissed_text.clear();
}

void kura_rt_tooltip_draw() {
  auto& state = tl_state;
  state.pending = false;

  KuraRect anchor{};
  KuraValue message{};
  const auto over = state.has_pointer &&
                    kura_rt_event_topmost(kura::rt::kEventTooltip, state.pointer_x, state.pointer_y, &anchor, &message);
  if (!over || message.tag != kura::rt::kTagString) {
    state.hovering = false;
    state.dismissed = false;
    return;
  }

  const std::string text(kura::rt::StringView(message.bits));
  if (!state.hovering || !Same(anchor, text, state.box, state.text)) {
    // over something new: it starts being waited for, and what was dismissed is forgotten
    state.hovering = true;
    state.box = anchor;
    state.text = text;
    state.since = Now();
    if (state.dismissed && !Same(anchor, text, state.dismissed_box, state.dismissed_text))
      state.dismissed = false;
  }

  if (state.dismissed || text.empty())
    return;

  // a clock that goes back (a host that starts its time over) can't have the pointer waiting from the future
  if (Now() < state.since)
    state.since = Now();
  if (Now() - state.since < state.style.delay) {
    state.pending = true;
    return;
  }

  const auto viewport_width = static_cast<float>(kura_rt_viewport_width());
  const auto viewport_height = static_cast<float>(kura_rt_viewport_height());
  const auto& style = state.style;

  // The text is wrapped to what is left of the widest a tooltip is, or of the viewport if that is less.
  const auto widest =
      std::max(1.0f, std::min(style.max_width, viewport_width - 2 * style.margin) - 2 * style.padding_x);
  const auto lines = kura::rt::WrapText(text, style.font_size, "", widest);
  float text_width = 0;
  float text_height = 0;
  for (const auto& line : lines) {
    text_width = std::max(text_width, line.width);
    text_height += line.height;
  }
  const auto width = text_width + 2 * style.padding_x;
  const auto height = text_height + 2 * style.padding_y;

  // Under the node, with the pointer in the middle of it; above the node if there is no room below, and always
  // inside the viewport.
  auto x = static_cast<float>(state.pointer_x) - width / 2;
  auto y = anchor.y + anchor.height + style.gap;
  if (y + height > viewport_height - style.margin)
    y = anchor.y - style.gap - height;
  x = std::max(style.margin, std::min(x, viewport_width - width - style.margin));
  y = std::max(style.margin, std::min(y, viewport_height - height - style.margin));

  constexpr KuraColor kNoBorder{0.0f, 0.0f, 0.0f, 0.0f};
  const KuraRect box{x, y, width, height};
  kura_rt_paint_box(&box, &style.background, &kNoBorder, Bits(0), Bits(style.radius), Bits(1));
  auto line_y = y + style.padding_y;
  for (const auto& line : lines) {
    if (!line.text.empty()) {
      kura_rt_render_text(x + style.padding_x, line_y, line.text.data(), line.text.size(), style.font_size, nullptr, 0,
                          &style.color);
    }
    line_y += line.height;
  }
}
}  // extern "C"
