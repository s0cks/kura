#include "layout/layout.h"

#include <algorithm>
#include <bit>
#include <cstring>

namespace kura::layout {
namespace {
// Lines wrap when they overflow by more than this; float noise must not push an item to the next line.
static constexpr Scalar kEpsilon = 1e-4f;

static inline auto Resolve(const Length& length, const Scalar base) -> Scalar {
  switch (length.unit) {
    case Unit::kPx:
      return length.value;
    case Unit::kPct:
      return IsDefined(base) ? base * length.value * 0.01f : kUndefined;
    case Unit::kAuto:
    default:
      return kUndefined;
  }
}

static inline auto ResolveOr(const Length& length, const Scalar base, const Scalar fallback) -> Scalar {
  const auto value = Resolve(length, base);
  return IsDefined(value) ? value : fallback;
}

// The minimum wins over the maximum, as in CSS. Either bound may be undefined.
static inline auto Clamp(Scalar value, const Scalar min, const Scalar max) -> Scalar {
  if (IsDefined(max) && value > max)
    value = max;
  if (IsDefined(min) && value < min)
    value = min;
  return value;
}

static inline auto Bits(const Scalar value) -> uint32_t {
  return IsDefined(value) ? std::bit_cast<uint32_t>(value) : 0x7fc00000u;
}

struct Padding {
  Scalar left = 0;
  Scalar top = 0;
  Scalar right = 0;
  Scalar bottom = 0;
};

static inline auto ResolvePadding(const Style& style, const Scalar base) -> Padding {
  return Padding{
      std::max(ResolveOr(style.padding.left, base, 0), 0.0f),
      std::max(ResolveOr(style.padding.top, base, 0), 0.0f),
      std::max(ResolveOr(style.padding.right, base, 0), 0.0f),
      std::max(ResolveOr(style.padding.bottom, base, 0), 0.0f),
  };
}

static inline auto IsRow(const Direction direction) -> bool {
  return direction == Direction::kRow || direction == Direction::kRowReverse;
}

static inline auto IsReverse(const Direction direction) -> bool {
  return direction == Direction::kRowReverse || direction == Direction::kColumnReverse;
}
}  // namespace

// A flex item, in the axes of its container: "main" runs along the container's direction, "cross" across it.
struct Engine::Item {
  NodeId id = kNoNode;
  Scalar margin_main_start = 0;
  Scalar margin_main_end = 0;
  Scalar margin_cross_start = 0;
  Scalar margin_cross_end = 0;
  Scalar min_main = 0;
  Scalar max_main = kUndefined;
  Scalar min_cross = 0;
  Scalar max_cross = kUndefined;
  Scalar pad_main = 0;
  Scalar style_cross = kUndefined;  // the cross size the style asks for, if it asks for one
  Align align = Align::kStretch;
  Scalar grow = 0;
  Scalar shrink = 1;
  Scalar basis = 0;  // flex base size
  Scalar hypothetical = 0;  // flex base size, clamped
  Scalar main = 0;  // resolved
  Scalar cross = 0;
  Scalar pos_main = 0;  // border box offset from the container's inner box
  Scalar pos_cross = 0;
  Scalar violation = 0;
  bool frozen = false;

  inline auto ClampMain(const Scalar value) const -> Scalar {
    return Clamp(value, min_main, max_main);
  }

  inline auto ClampCross(const Scalar value) const -> Scalar {
    return Clamp(value, min_cross, max_cross);
  }

  inline auto MainMargins() const -> Scalar {
    return margin_main_start + margin_main_end;
  }

  inline auto CrossMargins() const -> Scalar {
    return margin_cross_start + margin_cross_end;
  }
};

void Engine::CollectItems(const NodeId parent, std::vector<NodeId>* items, std::vector<NodeId>* contents) const {
  for (auto child = nodes_[parent].first_child; child != kNoNode; child = nodes_[child].next_sibling) {
    if (nodes_[child].style.display == Display::kContents) {
      contents->push_back(child);
      CollectItems(child, items, contents);
    } else {
      items->push_back(child);
    }
  }
}

// Lays out (when `commit`) or just measures (when not) the box of `id`.
//
//  * `kw`, `kh`: the border-box size the parent has decided on, or undefined for "whatever the content wants".
//  * `aw`, `ah`: the space on offer when the size is undefined; it only bounds what a measured leaf may wrap to.
//  * `pw`: what percentages in the node's own padding resolve against.
//  * `ox`, `oy`: where the box is; only read when committing.
//
// The result is the border-box size. It is not clamped to the node's min/max: whoever picks a size for a node
// (its parent, or `Run` for the root) clamps, because only they know what the percentages are relative to.
auto Engine::Compute(const NodeId id, const Scalar kw, const Scalar kh, const Scalar aw, const Scalar ah,
                     const Scalar pw, const bool commit, const Scalar ox, const Scalar oy) -> Size {
  if (commit)
    return ComputeUncached(id, kw, kh, aw, ah, pw, commit, ox, oy);
  auto& cache = cache_[id];
  if (cache.generation != generation_) {
    cache.generation = generation_;
    cache.count = 0;
    cache.next = 0;
  }
  const CacheKey key{Bits(kw), Bits(kh), Bits(aw), Bits(ah), Bits(pw)};
  for (auto i = 0; i < cache.count; ++i) {
    if (std::memcmp(&cache.entries[i].key, &key, sizeof(CacheKey)) == 0)
      return cache.entries[i].size;
  }
  const auto size = ComputeUncached(id, kw, kh, aw, ah, pw, commit, ox, oy);
  const auto slot = cache.count < kCacheEntries ? cache.count++ : cache.next++ % kCacheEntries;
  if (cache.next >= kCacheEntries)
    cache.next = 0;
  cache.entries[slot] = CacheEntry{key, size};
  return size;
}

auto Engine::ComputeUncached(const NodeId id, Scalar kw, Scalar kh, const Scalar aw, const Scalar ah,
                             const Scalar pw, const bool commit, const Scalar ox, const Scalar oy) -> Size {
  computes_++;
  const auto& node = nodes_[id];
  const auto& style = node.style;
  const auto pad = ResolvePadding(style, pw);
  const auto pad_w = pad.left + pad.right;
  const auto pad_h = pad.top + pad.bottom;
  if (IsDefined(kw))
    kw = std::max(kw, pad_w);
  if (IsDefined(kh))
    kh = std::max(kh, pad_h);
  const auto iw = IsDefined(kw) ? kw - pad_w : kUndefined;  // inner box
  const auto ih = IsDefined(kh) ? kh - pad_h : kUndefined;

  if (node.first_child == kNoNode) {
    auto width = kw;
    auto height = kh;
    if (!IsDefined(width) || !IsDefined(height)) {
      Size content{};
      if (node.measured && measure_ != nullptr) {
        const auto max_width = IsDefined(iw) ? iw : (IsDefined(aw) ? std::max(aw - pad_w, 0.0f) : kUndefined);
        content = measure_(user_, id, max_width);
      }
      if (!IsDefined(width))
        width = content.width + pad_w;
      if (!IsDefined(height))
        height = content.height + pad_h;
    }
    return Size{width, height};
  }

  const auto row = IsRow(style.direction);
  std::vector<NodeId> ids{};
  std::vector<NodeId> contents{};
  CollectItems(id, &ids, &contents);

  // The space the children may assume when this box has no size of its own yet.
  const auto avail_iw = IsDefined(iw) ? iw : (IsDefined(aw) ? std::max(aw - pad_w, 0.0f) : kUndefined);
  const auto avail_ih = IsDefined(ih) ? ih : (IsDefined(ah) ? std::max(ah - pad_h, 0.0f) : kUndefined);
  auto main_in = row ? iw : ih;
  auto cross_in = row ? ih : iw;
  const auto avail_main = row ? avail_iw : avail_ih;
  const auto avail_cross = row ? avail_ih : avail_iw;
  const auto gap_main = std::max(ResolveOr(style.gap, main_in, 0), 0.0f);
  const auto gap_cross = std::max(ResolveOr(style.gap, cross_in, 0), 0.0f);

  // 9.2: the flex base size and hypothetical main size of every item.
  std::vector<Item> items{};
  items.reserve(ids.size());
  for (const auto child : ids) {
    const auto& cs = nodes_[child].style;
    Item item{};
    item.id = child;
    const auto margin_left = ResolveOr(cs.margin.left, iw, 0);
    const auto margin_right = ResolveOr(cs.margin.right, iw, 0);
    const auto margin_top = ResolveOr(cs.margin.top, iw, 0);
    const auto margin_bottom = ResolveOr(cs.margin.bottom, iw, 0);
    item.margin_main_start = row ? margin_left : margin_top;
    item.margin_main_end = row ? margin_right : margin_bottom;
    item.margin_cross_start = row ? margin_top : margin_left;
    item.margin_cross_end = row ? margin_bottom : margin_right;

    const auto cp = ResolvePadding(cs, iw);
    const auto min_w = std::max(ResolveOr(cs.min_width, iw, 0), cp.left + cp.right);
    const auto min_h = std::max(ResolveOr(cs.min_height, ih, 0), cp.top + cp.bottom);
    const auto max_w = Resolve(cs.max_width, iw);
    const auto max_h = Resolve(cs.max_height, ih);
    const auto style_w = Resolve(cs.width, iw);
    const auto style_h = Resolve(cs.height, ih);
    item.min_main = row ? min_w : min_h;
    item.max_main = row ? max_w : max_h;
    item.min_cross = row ? min_h : min_w;
    item.max_cross = row ? max_h : max_w;
    item.pad_main = row ? cp.left + cp.right : cp.top + cp.bottom;
    item.style_cross = row ? style_h : style_w;
    item.align = cs.align_self == Align::kAuto ? style.align : cs.align_self;
    if (item.align == Align::kAuto)
      item.align = Align::kStretch;
    item.grow = std::max(cs.grow, 0.0f);
    item.shrink = std::max(cs.shrink, 0.0f);
    if (IsDefined(item.style_cross))
      item.style_cross = item.ClampCross(item.style_cross);

    // What cross size the item can be assumed to get, for measuring its content along the main axis. A
    // stretched item in a single-line container gets the whole cross space; otherwise it is unknown.
    auto hint = item.style_cross;
    if (!IsDefined(hint) && item.align == Align::kStretch && !style.wrap && IsDefined(cross_in))
      hint = item.ClampCross(std::max(cross_in - item.CrossMargins(), 0.0f));

    auto basis = kUndefined;
    if (!cs.basis.IsAuto())
      basis = Resolve(cs.basis, main_in);
    if (!IsDefined(basis))
      basis = row ? style_w : style_h;
    if (!IsDefined(basis)) {
      const auto content = Compute(child, row ? kUndefined : hint, row ? hint : kUndefined,
                                   row ? kUndefined : avail_cross, row ? avail_cross : kUndefined, iw, false, 0, 0);
      basis = row ? content.width : content.height;
    }
    item.basis = std::max(basis, item.pad_main);
    item.hypothetical = item.ClampMain(item.basis);
    items.push_back(item);
  }

  // 9.3: collect the items into lines. An undefined main size is the longest line, which for a container
  // that wraps is limited by the space on offer.
  struct Line {
    size_t begin = 0;
    size_t end = 0;
    Scalar used = 0;
    Scalar cross = 0;
    Scalar offset = 0;
  };
  std::vector<Line> lines{};
  {
    const auto break_at = IsDefined(main_in)
                              ? main_in
                              : (style.wrap && IsDefined(avail_main) ? avail_main : std::numeric_limits<Scalar>::infinity());
    Line line{};
    for (size_t i = 0; i < items.size(); ++i) {
      const auto outer = items[i].hypothetical + items[i].MainMargins();
      const auto added = (i > line.begin ? gap_main : 0) + outer;
      if (style.wrap && i > line.begin && line.used + added > break_at + kEpsilon) {
        line.end = i;
        lines.push_back(line);
        line = Line{};
        line.begin = i;
        line.used = outer;
      } else {
        line.used += added;
      }
    }
    if (!items.empty()) {
      line.end = items.size();
      lines.push_back(line);
    }
  }
  if (!IsDefined(main_in)) {
    main_in = 0;
    for (const auto& line : lines)
      main_in = std::max(main_in, line.used);
  }

  // 9.7: resolve flexible lengths, line by line.
  for (const auto& line : lines) {
    const auto n = line.end - line.begin;
    const auto total_gap = gap_main * static_cast<Scalar>(n - 1);
    auto sum_hypothetical = Scalar{0};
    for (auto i = line.begin; i < line.end; ++i)
      sum_hypothetical += items[i].hypothetical + items[i].MainMargins();
    const auto growing = main_in - total_gap - sum_hypothetical > 0;
    for (auto i = line.begin; i < line.end; ++i) {
      auto& item = items[i];
      const auto factor = growing ? item.grow : item.shrink;
      item.frozen = factor == 0 || (growing && item.basis > item.hypothetical) ||
                    (!growing && item.basis < item.hypothetical);
      item.main = item.frozen ? item.hypothetical : item.basis;
    }
    const auto free_space = [&]() {
      auto free = main_in - total_gap;
      for (auto i = line.begin; i < line.end; ++i)
        free -= (items[i].frozen ? items[i].main : items[i].basis) + items[i].MainMargins();
      return free;
    };
    const auto initial_free = free_space();
    for (size_t pass = 0; pass <= n; ++pass) {
      auto any = false;
      auto factor_sum = Scalar{0};
      auto scaled_sum = Scalar{0};
      for (auto i = line.begin; i < line.end; ++i) {
        if (items[i].frozen)
          continue;
        any = true;
        factor_sum += growing ? items[i].grow : items[i].shrink;
        scaled_sum += items[i].shrink * items[i].basis;
      }
      if (!any)
        break;
      auto remaining = free_space();
      if (factor_sum < 1 && std::fabs(initial_free * factor_sum) < std::fabs(remaining))
        remaining = initial_free * factor_sum;
      for (auto i = line.begin; i < line.end; ++i) {
        auto& item = items[i];
        if (item.frozen)
          continue;
        item.main = item.basis;
        if (remaining == 0)
          continue;
        if (growing && factor_sum > 0)
          item.main += remaining * item.grow / factor_sum;
        else if (!growing && scaled_sum > 0)
          item.main -= std::fabs(remaining) * (item.shrink * item.basis) / scaled_sum;
      }
      auto total_violation = Scalar{0};
      for (auto i = line.begin; i < line.end; ++i) {
        auto& item = items[i];
        if (item.frozen)
          continue;
        const auto clamped = item.ClampMain(item.main);
        item.violation = clamped - item.main;
        item.main = clamped;
        total_violation += item.violation;
      }
      for (auto i = line.begin; i < line.end; ++i) {
        auto& item = items[i];
        if (item.frozen)
          continue;
        if (total_violation == 0 || (total_violation > 0 && item.violation > 0) ||
            (total_violation < 0 && item.violation < 0))
          item.frozen = true;
      }
    }
  }

  // 9.4: cross sizes. An item that is going to be stretched (auto cross size, `align: stretch`) is sized by
  // its line below, and by the container itself when there is only one line.
  for (auto& item : items) {
    if (IsDefined(item.style_cross)) {
      item.cross = item.style_cross;
    } else if (item.align == Align::kStretch && !style.wrap && IsDefined(cross_in)) {
      item.cross = item.ClampCross(std::max(cross_in - item.CrossMargins(), 0.0f));
    } else {
      const auto content = Compute(item.id, row ? item.main : kUndefined, row ? kUndefined : item.main,
                                   row ? kUndefined : avail_cross, row ? avail_cross : kUndefined, iw, false, 0, 0);
      item.cross = item.ClampCross(row ? content.height : content.width);
    }
  }
  for (auto& line : lines) {
    if (!style.wrap && IsDefined(cross_in)) {
      line.cross = cross_in;
    } else {
      for (auto i = line.begin; i < line.end; ++i)
        line.cross = std::max(line.cross, items[i].cross + items[i].CrossMargins());
    }
  }
  {
    auto total = Scalar{0};
    for (const auto& line : lines)
      total += line.cross;
    if (!lines.empty())
      total += gap_cross * static_cast<Scalar>(lines.size() - 1);
    if (!IsDefined(cross_in)) {
      cross_in = total;
    } else if (style.wrap && !lines.empty() && cross_in > total) {
      // `align-content: stretch`
      const auto extra = (cross_in - total) / static_cast<Scalar>(lines.size());
      for (auto& line : lines)
        line.cross += extra;
    }
    auto offset = Scalar{0};
    for (auto& line : lines) {
      line.offset = offset;
      offset += line.cross + gap_cross;
    }
  }

  // 9.5 and 9.6: main-axis alignment (`justify`) and cross-axis alignment (`align`).
  const auto reverse = IsReverse(style.direction);
  for (const auto& line : lines) {
    const auto n = line.end - line.begin;
    auto used = gap_main * static_cast<Scalar>(n - 1);
    for (auto i = line.begin; i < line.end; ++i)
      used += items[i].main + items[i].MainMargins();
    const auto free = main_in - used;
    auto justify = style.justify;
    if (free < 0) {
      // overflowing: the distributing modes have nothing to distribute
      if (justify == Justify::kSpaceBetween)
        justify = Justify::kStart;
      else if (justify == Justify::kSpaceAround || justify == Justify::kSpaceEvenly)
        justify = Justify::kCenter;
    }
    auto lead = Scalar{0};
    auto between = Scalar{0};
    switch (justify) {
      case Justify::kCenter:
        lead = free / 2;
        break;
      case Justify::kEnd:
        lead = free;
        break;
      case Justify::kSpaceBetween:
        if (n > 1)
          between = free / static_cast<Scalar>(n - 1);
        break;
      case Justify::kSpaceAround:
        between = free / static_cast<Scalar>(n);
        lead = between / 2;
        break;
      case Justify::kSpaceEvenly:
        between = free / static_cast<Scalar>(n + 1);
        lead = between;
        break;
      case Justify::kStart:
      default:
        break;
    }
    auto position = lead;
    for (auto i = line.begin; i < line.end; ++i) {
      auto& item = items[i];
      item.pos_main = position + item.margin_main_start;
      position += item.margin_main_start + item.main + item.margin_main_end + gap_main + between;
      // The reverse directions are the forward layout mirrored across the inner box.
      if (reverse)
        item.pos_main = main_in - (item.pos_main + item.main);

      const auto space = line.cross - item.CrossMargins();
      if (item.align == Align::kStretch && !IsDefined(item.style_cross))
        item.cross = item.ClampCross(std::max(space, 0.0f));
      auto align_offset = Scalar{0};
      if (item.align == Align::kCenter)
        align_offset = (space - item.cross) / 2;
      else if (item.align == Align::kEnd)
        align_offset = space - item.cross;
      item.pos_cross = line.offset + item.margin_cross_start + align_offset;
    }
  }

  const auto width = IsDefined(kw) ? kw : (row ? main_in : cross_in) + pad_w;
  const auto height = IsDefined(kh) ? kh : (row ? cross_in : main_in) + pad_h;

  if (commit) {
    for (const auto child : contents)
      out_[child] = Box{ox + pad.left, oy + pad.top, 0, 0};
    for (const auto& item : items) {
      const auto x = ox + pad.left + (row ? item.pos_main : item.pos_cross);
      const auto y = oy + pad.top + (row ? item.pos_cross : item.pos_main);
      const auto w = row ? item.main : item.cross;
      const auto h = row ? item.cross : item.main;
      out_[item.id] = Box{x, y, w, h};
      if (nodes_[item.id].first_child != kNoNode)
        Compute(item.id, w, h, kUndefined, kUndefined, width - pad_w, true, x, y);
    }
  }
  return Size{width, height};
}

auto Engine::FixContents(const NodeId id) -> bool {
  const auto contents = id != root_ && nodes_[id].style.display == Display::kContents;
  auto any = false;
  auto left = std::numeric_limits<Scalar>::infinity();
  auto top = left;
  auto right = -left;
  auto bottom = -left;
  for (auto child = nodes_[id].first_child; child != kNoNode; child = nodes_[child].next_sibling) {
    if (!FixContents(child))
      continue;
    const auto& box = out_[child];
    left = std::min(left, box.x);
    top = std::min(top, box.y);
    right = std::max(right, box.x + box.width);
    bottom = std::max(bottom, box.y + box.height);
    any = true;
  }
  if (!contents)
    return true;
  if (!any)
    return false;
  out_[id] = Box{left, top, right - left, bottom - top};
  return true;
}

void Engine::Run(const std::span<const Node> nodes, const NodeId root, const Size viewport, const std::span<Box> out,
                 const MeasureFn measure, void* user) {
  generation_++;
  computes_ = 0;
  if (out.size() < nodes.size())
    return;
  std::fill(out.begin(), out.end(), Box{});
  if (root >= nodes.size())
    return;
  nodes_ = nodes;
  out_ = out;
  root_ = root;
  measure_ = measure;
  user_ = user;
  if (cache_.size() < nodes.size())
    cache_.resize(nodes.size());

  // The root is sized like a stretched flex item of a viewport-sized container.
  const auto& style = nodes[root].style;
  const auto vw = viewport.width;
  const auto vh = viewport.height;
  const auto pad = ResolvePadding(style, vw);
  const auto margin_left = ResolveOr(style.margin.left, vw, 0);
  const auto margin_right = ResolveOr(style.margin.right, vw, 0);
  const auto margin_top = ResolveOr(style.margin.top, vw, 0);
  const auto margin_bottom = ResolveOr(style.margin.bottom, vw, 0);
  auto width = Resolve(style.width, vw);
  if (!IsDefined(width))
    width = std::max(vw - margin_left - margin_right, 0.0f);
  auto height = Resolve(style.height, vh);
  if (!IsDefined(height))
    height = std::max(vh - margin_top - margin_bottom, 0.0f);
  width = Clamp(width, std::max(ResolveOr(style.min_width, vw, 0), pad.left + pad.right),
                Resolve(style.max_width, vw));
  height = Clamp(height, std::max(ResolveOr(style.min_height, vh, 0), pad.top + pad.bottom),
                 Resolve(style.max_height, vh));
  out_[root] = Box{margin_left, margin_top, width, height};
  Compute(root, width, height, kUndefined, kUndefined, vw, true, margin_left, margin_top);
  FixContents(root);
}

void Compute(const std::span<const Node> nodes, const NodeId root, const Size viewport, const std::span<Box> out,
             const MeasureFn measure, void* user) {
  Engine engine{};
  engine.Run(nodes, root, viewport, out, measure, user);
}

auto AddNode(std::vector<Node>* nodes, const NodeId parent, const Style& style, const bool measured) -> NodeId {
  const auto id = static_cast<NodeId>(nodes->size());
  Node node{};
  node.style = style;
  node.parent = parent;
  node.measured = measured;
  nodes->push_back(node);
  if (parent != kNoNode) {
    auto& p = (*nodes)[parent];
    if (p.first_child == kNoNode) {
      p.first_child = id;
    } else {
      auto last = p.first_child;
      while ((*nodes)[last].next_sibling != kNoNode)
        last = (*nodes)[last].next_sibling;
      (*nodes)[last].next_sibling = id;
    }
  }
  return id;
}
}  // namespace kura::layout
