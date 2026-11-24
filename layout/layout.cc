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
void Engine::CollectItems(const NodeId parent, std::vector<NodeId>* items, std::vector<NodeId>* contents) {
  for (auto child = nodes_[parent].first_child; child != kNoNode; child = nodes_[child].next_sibling) {
    if (nodes_[child].style.display == Display::kContents) {
      has_contents_ = true;
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

auto Engine::ComputeUncached(const NodeId id, Scalar kw, Scalar kh, const Scalar aw, const Scalar ah, const Scalar pw,
                             const bool commit, const Scalar ox, const Scalar oy) -> Size {
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

  const size_t ids_base = scratch_ids_.size();
  const size_t contents_base = scratch_contents_.size();
  const size_t items_base = scratch_items_.size();
  const size_t lines_base = scratch_lines_.size();

  struct ScratchGuard {
    Engine* engine;
    size_t ids_base;
    size_t contents_base;
    size_t items_base;
    size_t lines_base;
    ~ScratchGuard() {
      if (engine->scratch_ids_.size() != ids_base)
        engine->scratch_ids_.resize(ids_base);

      if (engine->scratch_contents_.size() != contents_base)
        engine->scratch_contents_.resize(contents_base);

      if (engine->scratch_items_.size() != items_base)
        engine->scratch_items_.resize(items_base);

      if (engine->scratch_lines_.size() != lines_base)
        engine->scratch_lines_.resize(lines_base);
    }
  } guard{this, ids_base, contents_base, items_base, lines_base};

  const auto row = IsRow(style.direction);
  CollectItems(id, &scratch_ids_, &scratch_contents_);
  const size_t ids_end = scratch_ids_.size();
  const size_t contents_end = scratch_contents_.size();
  const size_t item_count = ids_end - ids_base;

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
  bool any_flexible = false;
  for (size_t idx = 0; idx < ids_end - ids_base; ++idx) {
    const auto child = scratch_ids_[ids_base + idx];
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
      const auto content = SizeChild(child, row ? kUndefined : hint, row ? hint : kUndefined,
                                     row ? kUndefined : avail_cross, row ? avail_cross : kUndefined, iw);
      basis = row ? content.width : content.height;
    }
    item.basis = std::max(basis, item.pad_main);
    item.hypothetical = item.ClampMain(item.basis);

    scratch_items_.push_back(item);
    any_flexible |= (item.grow > 0 || item.shrink > 0);
  }

  // 9.3: collect the items into lines.
  Line stack_line{};
  size_t line_count = 0;

  // 9.3: collect the items into lines.
  if (!style.wrap) {
    if (item_count > 0) {
      Scalar used = 0;
      for (size_t i = 0; i < item_count; ++i) {
        const auto& item = scratch_items_[items_base + i];
        used += (i > 0 ? gap_main : 0) + item.hypothetical + item.MainMargins();
      }
      stack_line = Line{0, item_count, used, 0, 0};
      line_count = 1;
    }
  } else {
    const auto break_at =
        IsDefined(main_in) ? main_in : (IsDefined(avail_main) ? avail_main : std::numeric_limits<Scalar>::infinity());
    Line line{};
    for (size_t i = 0; i < item_count; ++i) {
      const auto& item = scratch_items_[items_base + i];
      const auto outer = item.hypothetical + item.MainMargins();
      const auto added = (i > line.begin ? gap_main : 0) + outer;
      if (i > line.begin && line.used + added > break_at + kEpsilon) {
        line.end = i;
        scratch_lines_.push_back(line);
        line = Line{};
        line.begin = i;
        line.used = outer;
      } else {
        line.used += added;
      }
    }
    if (item_count > 0) {
      line.end = item_count;
      scratch_lines_.push_back(line);
    }
    line_count = scratch_lines_.size() - lines_base;
  }

  const auto line_at = [&](const size_t l) -> Line& {
    return style.wrap ? scratch_lines_[lines_base + l] : stack_line;
  };
  const size_t lines_end = scratch_lines_.size();
  if (!IsDefined(main_in)) {
    main_in = 0;
    for (size_t l = 0; l < line_count; ++l)
      main_in = std::max(main_in, line_at(l).used);
  }

  // 9.7: resolve flexible lengths, line by line.
  for (size_t l = 0; l < line_count; ++l) {
    const auto& line = line_at(l);
    const auto n = line.end - line.begin;
    const auto total_gap = gap_main * static_cast<Scalar>(n - 1);
    auto sum_hypothetical = Scalar{0};
    for (auto i = line.begin; i < line.end; ++i) {
      const auto& item = scratch_items_[items_base + i];
      sum_hypothetical += item.hypothetical + item.MainMargins();
    }
    const auto growing = main_in - total_gap - sum_hypothetical > 0;
    for (auto i = line.begin; i < line.end; ++i) {
      auto& item = scratch_items_[items_base + i];
      const auto factor = growing ? item.grow : item.shrink;
      item.frozen =
          factor == 0 || (growing && item.basis > item.hypothetical) || (!growing && item.basis < item.hypothetical);
      item.main = item.frozen ? item.hypothetical : item.basis;
    }
    const auto free_space = [&]() {
      auto free = main_in - total_gap;
      for (auto i = line.begin; i < line.end; ++i) {
        const auto& item = scratch_items_[items_base + i];
        free -= (item.frozen ? item.main : item.basis) + item.MainMargins();
      }
      return free;
    };
    const auto initial_free = free_space();
    if (any_flexible && initial_free != 0) {
      for (size_t pass = 0; pass <= n; ++pass) {
        auto any = false;
        auto factor_sum = Scalar{0};
        auto scaled_sum = Scalar{0};
        for (auto i = line.begin; i < line.end; ++i) {
          const auto& item = scratch_items_[items_base + i];
          if (item.frozen)
            continue;
          any = true;
          factor_sum += growing ? item.grow : item.shrink;
          scaled_sum += item.shrink * item.basis;
        }
        if (!any)
          break;
        auto remaining = free_space();
        if (factor_sum < 1 && std::fabs(initial_free * factor_sum) < std::fabs(remaining))
          remaining = initial_free * factor_sum;
        for (auto i = line.begin; i < line.end; ++i) {
          auto& item = scratch_items_[items_base + i];
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
          auto& item = scratch_items_[items_base + i];
          if (item.frozen)
            continue;
          const auto clamped = item.ClampMain(item.main);
          item.violation = clamped - item.main;
          item.main = clamped;
          total_violation += item.violation;
        }
        for (auto i = line.begin; i < line.end; ++i) {
          auto& item = scratch_items_[items_base + i];
          if (item.frozen)
            continue;
          if (total_violation == 0 || (total_violation > 0 && item.violation > 0) ||
              (total_violation < 0 && item.violation < 0))
            item.frozen = true;
        }
      }
    }
  }

  // 9.4: cross sizes (be careful not to hold a reference to `item` across `Compute(...)`!)
  for (size_t idx = 0; idx < item_count; ++idx) {
    if (IsDefined(scratch_items_[items_base + idx].style_cross)) {
      scratch_items_[items_base + idx].cross = scratch_items_[items_base + idx].style_cross;
    } else if (scratch_items_[items_base + idx].align == Align::kStretch && !style.wrap && IsDefined(cross_in)) {
      scratch_items_[items_base + idx].cross = scratch_items_[items_base + idx].ClampCross(
          std::max(cross_in - scratch_items_[items_base + idx].CrossMargins(), 0.0f));
    } else {
      const NodeId child_id = scratch_items_[items_base + idx].id;
      const Scalar child_main = scratch_items_[items_base + idx].main;
      const auto content = SizeChild(child_id, row ? child_main : kUndefined, row ? kUndefined : child_main,
                                     row ? kUndefined : avail_cross, row ? avail_cross : kUndefined, iw);
      scratch_items_[items_base + idx].cross =
          scratch_items_[items_base + idx].ClampCross(row ? content.height : content.width);
    }
  }
  for (size_t l = 0; l < line_count; ++l) {
    auto& line = line_at(l);
    if (!style.wrap && IsDefined(cross_in)) {
      line.cross = cross_in;
    } else {
      for (auto i = line.begin; i < line.end; ++i) {
        const auto& item = scratch_items_[items_base + i];
        line.cross = std::max(line.cross, item.cross + item.CrossMargins());
      }
    }
  }
  {
    auto total = Scalar{0};
    for (size_t l = 0; l < line_count; ++l)
      total += line_at(l).cross;
    if (line_count > 0)
      total += gap_cross * static_cast<Scalar>(line_count - 1);
    if (!IsDefined(cross_in)) {
      cross_in = total;
    } else if (style.wrap && line_count > 0 && cross_in > total) {
      const auto extra = (cross_in - total) / static_cast<Scalar>(line_count);
      for (size_t l = 0; l < line_count; ++l)
        line_at(l).cross += extra;
    }
    auto offset = Scalar{0};
    for (size_t l = 0; l < line_count; ++l) {
      line_at(l).offset = offset;
      offset += line_at(l).cross + gap_cross;
    }
  }

  // 9.5 and 9.6: main-axis alignment (`justify`) and cross-axis alignment (`align`).
  const auto reverse = IsReverse(style.direction);
  for (size_t l = 0; l < line_count; ++l) {
    const auto& line = line_at(l);
    const auto n = line.end - line.begin;
    auto used = gap_main * static_cast<Scalar>(n - 1);
    for (auto i = line.begin; i < line.end; ++i) {
      const auto& item = scratch_items_[items_base + i];
      used += item.main + item.MainMargins();
    }
    const auto free = main_in - used;
    auto justify = style.justify;
    if (free < 0) {
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
      auto& item = scratch_items_[items_base + i];
      item.pos_main = position + item.margin_main_start;
      position += item.margin_main_start + item.main + item.margin_main_end + gap_main + between;
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
    for (size_t idx = contents_base; idx < contents_end; ++idx)
      out_[scratch_contents_[idx]] = Box{ox + pad.left, oy + pad.top, 0, 0};
    for (size_t idx = 0; idx < ids_end - ids_base; ++idx) {
      const auto& item = scratch_items_[items_base + idx];
      const auto child_id = item.id;
      const auto x = ox + pad.left + (row ? item.pos_main : item.pos_cross);
      const auto y = oy + pad.top + (row ? item.pos_cross : item.pos_main);
      const auto w = row ? item.main : item.cross;
      const auto h = row ? item.cross : item.main;
      out_[child_id] = Box{x, y, w, h};
      if (nodes_[child_id].first_child != kNoNode)
        Compute(child_id, w, h, kUndefined, kUndefined, width - pad_w, true, x, y);
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
  std::memset(out.data(), 0, nodes.size() * sizeof(Box));
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
  width = Clamp(width, std::max(ResolveOr(style.min_width, vw, 0), pad.left + pad.right), Resolve(style.max_width, vw));
  height =
      Clamp(height, std::max(ResolveOr(style.min_height, vh, 0), pad.top + pad.bottom), Resolve(style.max_height, vh));
  out_[root] = Box{margin_left, margin_top, width, height};
  scratch_ids_.clear();
  scratch_contents_.clear();
  scratch_items_.clear();
  scratch_lines_.clear();
  if (scratch_ids_.capacity() < nodes.size())
    scratch_ids_.reserve(nodes.size());
  if (scratch_items_.capacity() < nodes.size())
    scratch_items_.reserve(nodes.size());
  if (scratch_lines_.capacity() < (nodes.size() / 4 + 16))
    scratch_lines_.reserve(nodes.size() / 4 + 16);
  Compute(root, width, height, kUndefined, kUndefined, vw, true, margin_left, margin_top);
  if (has_contents_)
    FixContents(root);
}

auto Engine::SizeChild(const NodeId id, const Scalar kw, const Scalar kh, const Scalar aw, const Scalar ah,
                       const Scalar pw) -> Size {
  if (nodes_[id].first_child == kNoNode)
    return MeasureLeaf(id, kw, kh, aw, ah, pw);
  return Compute(id, kw, kh, aw, ah, pw, false, 0, 0);
}

auto Engine::MeasureLeaf(const NodeId id, Scalar kw, Scalar kh, const Scalar aw, const Scalar ah, const Scalar pw)
    -> Size {
  const auto& node = nodes_[id];
  const auto& style = node.style;
  const auto pad = ResolvePadding(style, pw);
  const auto pad_w = pad.left + pad.right;
  const auto pad_h = pad.top + pad.bottom;
  if (IsDefined(kw))
    kw = std::max(kw, pad_w);
  if (IsDefined(kh))
    kh = std::max(kh, pad_h);
  const auto iw = IsDefined(kw) ? kw - pad_w : kUndefined;

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
