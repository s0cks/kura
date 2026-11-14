#ifndef KURA_LAYOUT_LAYOUT_H
#define KURA_LAYOUT_LAYOUT_H

// The flexbox layout kernel.
//
// A pure function from a table of nodes (a tree, stored flat and indexed by node id) and a viewport to a
// table of boxes (x, y, width, height, indexed by the same ids). It has no dependency on the rest of Kura:
// the compiler emits the node table as constant data, the generated reflow stub patches the slots that
// depend on runtime state and calls the kernel, and `view` reads the boxes back by node id.
//
// Semantics follow CSS flexbox (css-flexbox-1, sections 9.2-9.7) with these deliberate differences:
//
//  * Every box is border-box: `width`/`height` include padding, and so do min/max and `basis`.
//  * `border-width` is paint only; it takes no space.
//  * An automatic minimum size is 0, not the item's min-content size (CSS `min-width: auto`). Items shrink
//    below their content unless `min-*` says otherwise.
//  * Percentages for `padding`, `margin`, `width`, `height`, `min-*`, `max-*`, `basis` and `gap` resolve
//    against the container's inner box (CSS resolves vertical padding/margin against the width too; this
//    does the same). A percentage whose base is not definite behaves as `auto` (as 0 for padding, margin
//    and gap).
//  * There is no `fr` unit here. The compiler lowers `width: 1fr` to `grow: 1` with a `basis` of 0.
//  * Boxes are not pixel-snapped; that is the renderer's business.
//  * Defaults are CSS's: `direction: row`, `align: stretch`, `justify: start`, `wrap: false`, `grow: 0`,
//    `shrink: 1`.
//  * A wrapping container behaves as if `align-content: stretch`: lines share any leftover cross space.
//
// Output coordinates are absolute: relative to the origin of the viewport, not to the parent.
//
// `Display::kContents` nodes (a `#fragment`) take no box of their own. Their children are laid out as
// children of the nearest ancestor that does take one, exactly like CSS `display: contents`. A contents
// node's box in the output is the union of its children's, which is only there for debugging.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

namespace kura::layout {
using Scalar = float;
using NodeId = uint64_t;

static constexpr NodeId kNoNode = std::numeric_limits<NodeId>::max();

// "No value": an unconstrained available size, an unknown size, a missing percentage base.
static constexpr Scalar kUndefined = std::numeric_limits<Scalar>::quiet_NaN();

static inline auto IsDefined(const Scalar value) -> bool {
  return !std::isnan(value);
}

enum class Unit : uint8_t {
  kAuto = 0,
  kPx,
  kPct,  // 0-100, so `50%` is stored as 50
};

struct Length {
  Scalar value = 0;
  Unit unit = Unit::kAuto;

  static inline constexpr auto Auto() -> Length {
    return Length{0, Unit::kAuto};
  }

  static inline constexpr auto Px(const Scalar value) -> Length {
    return Length{value, Unit::kPx};
  }

  static inline constexpr auto Pct(const Scalar value) -> Length {
    return Length{value, Unit::kPct};
  }

  inline constexpr auto IsAuto() const -> bool {
    return unit == Unit::kAuto;
  }
};

struct Edges {
  Length top = Length::Px(0);
  Length right = Length::Px(0);
  Length bottom = Length::Px(0);
  Length left = Length::Px(0);

  static inline constexpr auto All(const Length value) -> Edges {
    return Edges{value, value, value, value};
  }
};

enum class Direction : uint8_t {
  kRow = 0,
  kColumn,
  kRowReverse,
  kColumnReverse,
};

enum class Align : uint8_t {
  kAuto = 0,  // `align-self` only: use the container's `align`
  kStart,
  kCenter,
  kEnd,
  kStretch,
};

enum class Justify : uint8_t {
  kStart = 0,
  kCenter,
  kEnd,
  kSpaceBetween,
  kSpaceAround,
  kSpaceEvenly,
};

enum class Display : uint8_t {
  kFlex = 0,
  kContents,
};

// A node that scrolls is two nodes in a table: the viewport, which has this, and right after it its content, which
// is laid out at the size it wants and so may be larger than the viewport. The kernel lays both out like any other
// nodes; it is whoever runs it that moves the content by how far the viewport has been scrolled.
enum class Overflow : uint8_t {
  kVisible = 0,
  kScroll,
};

// The style properties that can change at run time. The compiler emits them as numbers, and the runtime
// applies them to the node table before a layout.
enum class Property : uint64_t {
  kWidth = 0,
  kHeight,
  kMinWidth,
  kMinHeight,
  kMaxWidth,
  kMaxHeight,
  kBasis,
  kPadding,
  kMargin,
  kGap,
  kDirection,
  kAlign,
  kAlignSelf,
  kJustify,
  kWrap,
  kGrow,
  kShrink,
};

struct Style {
  Length width = Length::Auto();
  Length height = Length::Auto();
  Length min_width = Length::Auto();  // auto: no minimum beyond the padding
  Length min_height = Length::Auto();
  Length max_width = Length::Auto();  // auto: no maximum
  Length max_height = Length::Auto();
  Length basis = Length::Auto();  // auto: the main-axis size if it has one, else the content
  Edges padding{};
  Edges margin{};
  Length gap = Length::Px(0);
  Scalar grow = 0;
  Scalar shrink = 1;
  Direction direction = Direction::kRow;
  Align align = Align::kStretch;
  Align align_self = Align::kAuto;
  Justify justify = Justify::kStart;
  bool wrap = false;
  Display display = Display::kFlex;
  Overflow overflow = Overflow::kVisible;
};

struct Node {
  Style style{};
  NodeId parent = kNoNode;
  NodeId first_child = kNoNode;
  NodeId next_sibling = kNoNode;
  // A leaf whose size comes from the host (text, an image). It is asked through `MeasureFn`.
  bool measured = false;
};

struct Box {
  Scalar x = 0;
  Scalar y = 0;
  Scalar width = 0;
  Scalar height = 0;
};

struct Size {
  Scalar width = 0;
  Scalar height = 0;
};

// These cross the generated-code boundary as raw bytes: keep them plain.
static_assert(std::is_standard_layout_v<Length>);
static_assert(std::is_standard_layout_v<Edges>);
static_assert(std::is_standard_layout_v<Style>);
static_assert(std::is_standard_layout_v<Node>);
static_assert(std::is_standard_layout_v<Box>);
static_assert(std::is_trivially_copyable_v<Style>);
static_assert(std::is_trivially_copyable_v<Node>);
static_assert(std::is_trivially_copyable_v<Box>);
static_assert(sizeof(Box) == 4 * sizeof(Scalar));

// The size of a measured node's content, not counting its padding. `max_width` is the widest the content may
// be, or `kUndefined` when it is free to take its natural size (text then does not wrap).
using MeasureFn = Size (*)(void* user, NodeId id, Scalar max_width);

class Engine {
 public:
  Engine() = default;
  ~Engine() = default;
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  // Lays out the tree under `root`. `out` must have one slot per node. The root fills the viewport unless its
  // own style says otherwise. Every slot not reachable from the root is zeroed.
  void Run(std::span<const Node> nodes, NodeId root, Size viewport, std::span<Box> out, MeasureFn measure = nullptr,
           void* user = nullptr);

  inline auto GetNumberOfRuns() const -> uint64_t {
    return generation_;
  }

  // How many times a container was laid out or measured by the last run: a cost counter for tests.
  inline auto GetNumberOfComputes() const -> uint64_t {
    return computes_;
  }

 private:
  struct CacheKey {
    uint32_t kw, kh, aw, ah, pw;
  };

  struct CacheEntry {
    CacheKey key{};
    Size size{};
  };

  static constexpr size_t kCacheEntries = 4;

  struct Cache {
    uint64_t generation = 0;
    uint8_t count = 0;
    uint8_t next = 0;
    CacheEntry entries[kCacheEntries]{};
  };

  struct Item;

  std::span<const Node> nodes_{};
  std::span<Box> out_{};
  NodeId root_ = kNoNode;
  MeasureFn measure_ = nullptr;
  void* user_ = nullptr;
  std::vector<Cache> cache_{};
  uint64_t generation_ = 0;
  uint64_t computes_ = 0;

  auto Compute(NodeId id, Scalar kw, Scalar kh, Scalar aw, Scalar ah, Scalar pw, bool commit, Scalar ox, Scalar oy)
      -> Size;
  auto ComputeUncached(NodeId id, Scalar kw, Scalar kh, Scalar aw, Scalar ah, Scalar pw, bool commit, Scalar ox,
                       Scalar oy) -> Size;
  void CollectItems(NodeId parent, std::vector<NodeId>* items, std::vector<NodeId>* contents) const;
  auto FixContents(NodeId id) -> bool;
};

// One-shot convenience for callers that don't keep an Engine around.
void Compute(std::span<const Node> nodes, NodeId root, Size viewport, std::span<Box> out, MeasureFn measure = nullptr,
             void* user = nullptr);

// Appends a node to a table, linking it as the last child of `parent` (or as a root when `parent` is
// `kNoNode`). Returns its id, which is its index. Ids come out in pre-order when children are added in order.
auto AddNode(std::vector<Node>* nodes, NodeId parent, const Style& style = {}, bool measured = false) -> NodeId;
}  // namespace kura::layout

#endif  // KURA_LAYOUT_LAYOUT_H
