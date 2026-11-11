// The layout side of the runtime ABI: what compiled code calls to lay a view out. See runtime_abi.h.
//
// Each DOM tree the compiler lowers gets a constant table of layout nodes. The first time a table
// is used on a thread it is copied into a Context, which then keeps the tree's text, its boxes and
// whether they are out of date. A frame only runs the layout kernel again when a text changed or
// the viewport did, so an update that doesn't touch layout costs a handful of comparisons.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "backend/runtime_abi.h"
#include "layout/layout.h"
#include "object.h"

namespace {
namespace lay = kura::layout;

constexpr float kDefaultFontSize = 16.0f;

struct Context {
  std::vector<lay::Node> nodes{};
  std::vector<lay::Box> boxes{};
  std::vector<std::string> texts{};
  std::vector<float> sizes{};
  std::vector<std::string> families{};
  lay::Engine engine{};
  lay::Size viewport{lay::kUndefined, lay::kUndefined};
  bool dirty = true;
};

thread_local std::unordered_map<const void*, std::unique_ptr<Context>> tl_contexts{};

auto ContextFor(const void* table, const uint64_t count) -> Context& {
  auto& slot = tl_contexts[table];
  if (slot && slot->nodes.size() == count)
    return *slot;
  slot = std::make_unique<Context>();
  slot->nodes.resize(count);
  if (count > 0)
    std::memcpy(slot->nodes.data(), table, count * sizeof(lay::Node));
  slot->boxes.resize(count);
  slot->texts.resize(count);
  slot->sizes.assign(count, kDefaultFontSize);
  slot->families.resize(count);
  return *slot;
}

// What the renderer can't tell us, when there is no renderer to ask: enough to keep layout working.
auto MeasureLine(const std::string& text, const float size, const std::string& family) -> lay::Size {
  float width = 0;
  float height = 0;
  if (kura_rt_measure_text(text.data(), text.size(), size, family.empty() ? nullptr : family.data(), family.size(),
                           &width, &height) != 0)
    return lay::Size{static_cast<float>(text.size()) * size * 0.5f, size * 1.2f};
  return lay::Size{width, height};
}

// One line of a text that has been wrapped, and how big it is.
struct Line {
  std::string text{};
  float width = 0;
  float height = 0;
};

// A text's lines: all of it on one line when it fits in `max_width` (or there is none), and otherwise broken at
// word boundaries. A word longer than the line gets a line of its own and overflows it. Layout measures a text
// with this, and drawing places its lines with it, so what is drawn is what was made room for.
auto Wrap(const std::string& text, const float size, const std::string& family, const lay::Scalar max_width)
    -> std::vector<Line> {
  const auto whole = MeasureLine(text, size, family);
  if (!lay::IsDefined(max_width) || whole.width <= max_width)
    return {Line{text, whole.width, whole.height}};

  std::vector<Line> lines{};
  Line line{};
  size_t pos = 0;
  while (pos <= text.size()) {
    const auto end = std::min(text.find(' ', pos), text.size());
    const auto word = text.substr(pos, end - pos);
    pos = end + 1;
    const auto candidate = line.text.empty() ? word : line.text + " " + word;
    const auto measured = MeasureLine(candidate, size, family);
    if (line.text.empty() || measured.width <= max_width) {
      line = Line{candidate, measured.width, measured.height};
    } else {
      lines.push_back(std::move(line));
      const auto alone = MeasureLine(word, size, family);
      line = Line{word, alone.width, alone.height};
    }
  }
  lines.push_back(std::move(line));
  return lines;
}

// The size of a text node's content, wrapped at word boundaries to `max_width` when it is given.
auto Measure(void* user, const lay::NodeId id, const lay::Scalar max_width) -> lay::Size {
  const auto& context = *static_cast<Context*>(user);
  lay::Size size{};
  for (const auto& line : Wrap(context.texts[id], context.sizes[id], context.families[id], max_width)) {
    size.width = std::max(size.width, line.width);
    size.height += line.height;
  }
  return size;
}

auto AsDouble(const uint64_t bits) -> double {
  double value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

// A length from a tagged value: `Px`, `Pct`, `Auto` (the string), or a bare number, which is pixels.
auto ToLength(const uint64_t tag, const uint64_t bits, lay::Length* out) -> bool {
  switch (tag) {
    case kura::rt::kTagPx:
      *out = lay::Length::Px(static_cast<float>(AsDouble(bits)));
      return true;
    case kura::rt::kTagPct:
      *out = lay::Length::Pct(static_cast<float>(AsDouble(bits)));
      return true;
    case kura::rt::kTagFloat:
      *out = lay::Length::Px(static_cast<float>(AsDouble(bits)));
      return true;
    case kura::rt::kTagInt:
      *out = lay::Length::Px(static_cast<float>(static_cast<int64_t>(bits)));
      return true;
    case kura::rt::kTagString:
      if (static_cast<kura::String*>(reinterpret_cast<void*>(bits))->GetValue() != "auto")
        return false;
      *out = lay::Length::Auto();
      return true;
    default:
      return false;
  }
}

auto ToNumber(const uint64_t tag, const uint64_t bits, float* out) -> bool {
  if (tag == kura::rt::kTagInt)
    *out = static_cast<float>(static_cast<int64_t>(bits));
  else if (tag == kura::rt::kTagFloat || tag == kura::rt::kTagPx)
    *out = static_cast<float>(AsDouble(bits));
  else
    return false;
  return true;
}

auto ToAlign(const std::string& name, lay::Align* out) -> bool {
  if (name == "start")
    *out = lay::Align::kStart;
  else if (name == "center")
    *out = lay::Align::kCenter;
  else if (name == "end")
    *out = lay::Align::kEnd;
  else if (name == "stretch")
    *out = lay::Align::kStretch;
  else
    return false;
  return true;
}

template <typename T>
auto Assign(T* field, const T& value) -> bool {
  if (std::memcmp(field, &value, sizeof(T)) == 0)
    return false;
  *field = value;
  return true;
}

// Applies a value to the property of a style. Returns whether the style changed; `ok` says whether the
// value was one the property takes.
auto Apply(lay::Style* style, const lay::Property property, const uint64_t tag, const uint64_t bits, bool* ok) -> bool {
  *ok = true;
  lay::Length length{};
  float number = 0;
  std::string name{};
  if (tag == kura::rt::kTagString)
    name = static_cast<kura::String*>(reinterpret_cast<void*>(bits))->GetValue();

  switch (property) {
    case lay::Property::kWidth:
    case lay::Property::kHeight:
    case lay::Property::kBasis: {
      auto& target = property == lay::Property::kWidth    ? style->width
                     : property == lay::Property::kHeight ? style->height
                                                          : style->basis;
      if (tag == kura::rt::kTagFr) {
        // `1fr` is a share of the free space: it grows from nothing.
        number = static_cast<float>(AsDouble(bits));
        auto changed = Assign(&style->grow, number);
        changed |= Assign(&style->basis, lay::Length::Px(0));
        return changed;
      }
      if (!ToLength(tag, bits, &length))
        break;
      return Assign(&target, length);
    }
    case lay::Property::kMinWidth:
    case lay::Property::kMinHeight:
    case lay::Property::kMaxWidth:
    case lay::Property::kMaxHeight: {
      auto& target = property == lay::Property::kMinWidth    ? style->min_width
                     : property == lay::Property::kMinHeight ? style->min_height
                     : property == lay::Property::kMaxWidth  ? style->max_width
                                                             : style->max_height;
      if (!ToLength(tag, bits, &length))
        break;
      return Assign(&target, length);
    }
    case lay::Property::kPadding:
      if (!ToLength(tag, bits, &length))
        break;
      return Assign(&style->padding, lay::Edges::All(length));
    case lay::Property::kMargin:
      if (!ToLength(tag, bits, &length))
        break;
      return Assign(&style->margin, lay::Edges::All(length));
    case lay::Property::kGap:
      if (!ToLength(tag, bits, &length))
        break;
      return Assign(&style->gap, length);
    case lay::Property::kGrow:
    case lay::Property::kShrink:
      if (!ToNumber(tag, bits, &number))
        break;
      return Assign(property == lay::Property::kGrow ? &style->grow : &style->shrink, number);
    case lay::Property::kDirection: {
      lay::Direction direction;
      if (name == "row")
        direction = lay::Direction::kRow;
      else if (name == "column")
        direction = lay::Direction::kColumn;
      else if (name == "row-reverse")
        direction = lay::Direction::kRowReverse;
      else if (name == "column-reverse")
        direction = lay::Direction::kColumnReverse;
      else
        break;
      return Assign(&style->direction, direction);
    }
    case lay::Property::kAlign:
    case lay::Property::kAlignSelf: {
      lay::Align align;
      if (!ToAlign(name, &align))
        break;
      return Assign(property == lay::Property::kAlign ? &style->align : &style->align_self, align);
    }
    case lay::Property::kJustify: {
      lay::Justify justify;
      if (name == "start")
        justify = lay::Justify::kStart;
      else if (name == "center")
        justify = lay::Justify::kCenter;
      else if (name == "end")
        justify = lay::Justify::kEnd;
      else if (name == "space-between")
        justify = lay::Justify::kSpaceBetween;
      else if (name == "space-around")
        justify = lay::Justify::kSpaceAround;
      else if (name == "space-evenly")
        justify = lay::Justify::kSpaceEvenly;
      else
        break;
      return Assign(&style->justify, justify);
    }
    case lay::Property::kWrap:
      if (tag != kura::rt::kTagBool)
        break;
      return Assign(&style->wrap, bits != 0);
  }
  *ok = false;
  return false;
}
}  // namespace

extern "C" {
void kura_rt_layout_set(const void* table, const uint64_t count, const uint64_t node, const uint64_t property,
                        const uint64_t tag, const uint64_t bits) {
  auto& context = ContextFor(table, count);
  if (node >= count)
    return;
  auto ok = false;
  if (Apply(&context.nodes[node].style, static_cast<lay::Property>(property), tag, bits, &ok))
    context.dirty = true;
  if (!ok)
    kura_rt_panic("a layout property was given a value it doesn't take");
}

void kura_rt_layout_set_text(const void* table, const uint64_t count, const uint64_t node, void* text,
                             const double size, const char* family, const uint64_t family_length) {
  auto& context = ContextFor(table, count);
  if (node >= count)
    return;
  const auto& value = static_cast<kura::String*>(text)->GetValue();
  const auto font_size = static_cast<float>(size);
  const auto font_family = family == nullptr ? std::string{} : std::string(family, static_cast<size_t>(family_length));
  if (context.texts[node] == value && context.sizes[node] == font_size && context.families[node] == font_family)
    return;
  context.texts[node] = value;
  context.sizes[node] = font_size;
  context.families[node] = font_family;
  context.dirty = true;
}

void kura_rt_draw_text(const void* table, const uint64_t count, const uint64_t node, const uint64_t color) {
  auto& context = ContextFor(table, count);
  if (node >= count)
    return;
  const auto& text = context.texts[node];
  if (text.empty())
    return;

  // The text starts inside its padding. (A percentage padding is left out: it resolves against a width that
  // the box doesn't keep.)
  const auto& padding = context.nodes[node].style.padding;
  const auto pixels = [](const lay::Length& length) {
    return length.unit == lay::Unit::kPx ? std::max(length.value, 0.0f) : 0.0f;
  };
  const auto& box = context.boxes[node];
  const auto left = pixels(padding.left);
  const auto top = pixels(padding.top);
  // a hair of slack, so that a box that is exactly as wide as its text doesn't wrap it for rounding
  const auto room = box.width - left - pixels(padding.right) + 0.01f;

  const auto& family = context.families[node];
  auto y = box.y + top;
  for (const auto& line : Wrap(text, context.sizes[node], family, room)) {
    if (!line.text.empty()) {
      kura_rt_render_text(box.x + left, y, line.text.data(), line.text.size(), context.sizes[node],
                          family.empty() ? nullptr : family.data(), family.size(), color);
    }
    y += line.height;
  }
}

void kura_rt_layout_run(const void* table, const uint64_t count) {
  auto& context = ContextFor(table, count);
  const lay::Size viewport{static_cast<float>(kura_rt_viewport_width()), static_cast<float>(kura_rt_viewport_height())};
  if (!context.dirty && viewport.width == context.viewport.width && viewport.height == context.viewport.height)
    return;
  context.engine.Run(context.nodes, 0, viewport, context.boxes, &Measure, &context);
  context.viewport = viewport;
  context.dirty = false;
}

double kura_rt_layout_get(const void* table, const uint64_t count, const uint64_t node, const uint64_t component) {
  auto& context = ContextFor(table, count);
  if (node >= count)
    return 0;
  const auto& box = context.boxes[node];
  switch (component) {
    case 0:
      return box.x;
    case 1:
      return box.y;
    case 2:
      return box.width;
    case 3:
      return box.height;
    default:
      return 0;
  }
}
}
