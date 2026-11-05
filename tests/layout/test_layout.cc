#include <gtest/gtest.h>

#include <chrono>
#include <vector>

#include "layout/layout.h"

namespace kura::layout {
namespace {
using L = Length;

class LayoutTest : public ::testing::Test {
 protected:
  std::vector<Node> nodes{};
  std::vector<Box> boxes{};

  auto Add(const NodeId parent, const Style& style = {}, const bool measured = false) -> NodeId {
    return AddNode(&nodes, parent, style, measured);
  }

  void Run(const Scalar w, const Scalar h, const MeasureFn measure = nullptr, void* user = nullptr) {
    boxes.assign(nodes.size(), Box{});
    Compute(nodes, 0, Size{w, h}, boxes, measure, user);
  }

  void ExpectBox(const NodeId id, const Scalar x, const Scalar y, const Scalar w, const Scalar h) const {
    ASSERT_LT(id, boxes.size());
    EXPECT_NEAR(boxes[id].x, x, 1e-3) << "node " << id << " x";
    EXPECT_NEAR(boxes[id].y, y, 1e-3) << "node " << id << " y";
    EXPECT_NEAR(boxes[id].width, w, 1e-3) << "node " << id << " width";
    EXPECT_NEAR(boxes[id].height, h, 1e-3) << "node " << id << " height";
  }

  static auto Fixed(const Scalar w, const Scalar h) -> Style {
    Style s{};
    s.width = L::Px(w);
    s.height = L::Px(h);
    return s;
  }
};

TEST_F(LayoutTest, RootFillsViewport) {
  Add(kNoNode);
  Run(800, 600);
  ExpectBox(0, 0, 0, 800, 600);
}

TEST_F(LayoutTest, RootMarginAndExplicitSize) {
  Style root{};
  root.margin = Edges::All(L::Px(10));
  Add(kNoNode, root);
  Run(800, 600);
  ExpectBox(0, 10, 10, 780, 580);
}

TEST_F(LayoutTest, RowPacksAtStart) {
  Style root{};
  root.align = Align::kStart;
  Add(kNoNode, root);
  Add(0, Fixed(50, 20));
  Add(0, Fixed(60, 30));
  Add(0, Fixed(70, 10));
  Run(400, 100);
  ExpectBox(1, 0, 0, 50, 20);
  ExpectBox(2, 50, 0, 60, 30);
  ExpectBox(3, 110, 0, 70, 10);
}

TEST_F(LayoutTest, StretchFillsCrossAxis) {
  Add(kNoNode);
  Style item{};
  item.width = L::Px(50);
  Add(0, item);
  Run(400, 100);
  ExpectBox(1, 0, 0, 50, 100);
}

TEST_F(LayoutTest, GapAndPadding) {
  Style root{};
  root.padding = Edges::All(L::Px(10));
  root.gap = L::Px(5);
  root.align = Align::kStart;
  Add(kNoNode, root);
  Add(0, Fixed(50, 20));
  Add(0, Fixed(50, 20));
  Run(400, 100);
  ExpectBox(1, 10, 10, 50, 20);
  ExpectBox(2, 65, 10, 50, 20);
}

TEST_F(LayoutTest, ItemMargins) {
  Style root{};
  root.align = Align::kStart;
  Add(kNoNode, root);
  Style a = Fixed(50, 20);
  a.margin = Edges::All(L::Px(5));
  Add(0, a);
  Add(0, Fixed(50, 20));
  Run(400, 100);
  ExpectBox(1, 5, 5, 50, 20);
  ExpectBox(2, 60, 0, 50, 20);  // 5 + 50 + 5
}

struct JustifyCase {
  Justify justify;
  Scalar x0, x1, x2;
};

class JustifyTest : public LayoutTest, public ::testing::WithParamInterface<JustifyCase> {};

// three 50-wide items in 400: 250 free
TEST_P(JustifyTest, ThreeItems) {
  Style root{};
  root.justify = GetParam().justify;
  Add(kNoNode, root);
  for (auto i = 0; i < 3; ++i)
    Add(0, Fixed(50, 10));
  Run(400, 50);
  ExpectBox(1, GetParam().x0, 0, 50, 10);
  ExpectBox(2, GetParam().x1, 0, 50, 10);
  ExpectBox(3, GetParam().x2, 0, 50, 10);
}

INSTANTIATE_TEST_SUITE_P(All, JustifyTest,
                         ::testing::Values(JustifyCase{Justify::kStart, 0, 50, 100},
                                           JustifyCase{Justify::kCenter, 125, 175, 225},
                                           JustifyCase{Justify::kEnd, 250, 300, 350},
                                           JustifyCase{Justify::kSpaceBetween, 0, 175, 350},
                                           // 250/3 each side-pair: 41.667 around every item
                                           JustifyCase{Justify::kSpaceAround, 250.0f / 6, 250.0f / 6 * 3 + 50,
                                                       250.0f / 6 * 5 + 100},
                                           // 250/4 = 62.5 in every gap
                                           JustifyCase{Justify::kSpaceEvenly, 62.5, 175, 287.5}));

TEST_F(LayoutTest, OverflowFallsBackToStartForSpaceBetween) {
  Style root{};
  root.justify = Justify::kSpaceBetween;
  root.shrink = 1;
  Add(kNoNode, root);
  Style item = Fixed(300, 10);
  item.shrink = 0;
  Add(0, item);
  Add(0, item);
  Run(400, 50);
  ExpectBox(1, 0, 0, 300, 10);
  ExpectBox(2, 300, 0, 300, 10);
}

TEST_F(LayoutTest, SingleItemSpaceBetweenIsStart) {
  Style root{};
  root.justify = Justify::kSpaceBetween;
  Add(kNoNode, root);
  Add(0, Fixed(50, 10));
  Run(400, 50);
  ExpectBox(1, 0, 0, 50, 10);
}

TEST_F(LayoutTest, GrowSharesFreeSpaceByFactor) {
  Add(kNoNode);
  Style a{};
  a.basis = L::Px(0);
  a.grow = 1;
  Style b = a;
  b.grow = 2;
  Add(0, a);
  Add(0, b);
  Run(300, 50);
  ExpectBox(1, 0, 0, 100, 50);
  ExpectBox(2, 100, 0, 200, 50);
}

TEST_F(LayoutTest, GrowAddsToBasis) {
  Add(kNoNode);
  Style a{};
  a.basis = L::Px(30);
  a.grow = 1;
  Style b = a;
  b.grow = 2;
  Add(0, a);
  Add(0, b);
  Run(300, 50);
  // free = 300 - 60 = 240 -> 80 and 160
  ExpectBox(1, 0, 0, 110, 50);
  ExpectBox(2, 110, 0, 190, 50);
}

TEST_F(LayoutTest, ShrinkIsScaledByBasis) {
  Add(kNoNode);
  Add(0, Fixed(200, 10));
  Add(0, Fixed(100, 10));
  Run(150, 50);
  // overflow 150, scaled shrink 200:100 -> 100 and 50
  ExpectBox(1, 0, 0, 100, 10);
  ExpectBox(2, 100, 0, 50, 10);
}

TEST_F(LayoutTest, EqualShrink) {
  Add(kNoNode);
  for (auto i = 0; i < 3; ++i)
    Add(0, Fixed(200, 10));
  Run(300, 50);
  ExpectBox(1, 0, 0, 100, 10);
  ExpectBox(2, 100, 0, 100, 10);
  ExpectBox(3, 200, 0, 100, 10);
}

TEST_F(LayoutTest, ShrinkZeroKeepsSize) {
  Add(kNoNode);
  Style a = Fixed(200, 10);
  a.shrink = 0;
  Add(0, a);
  Add(0, Fixed(200, 10));
  Run(300, 50);
  ExpectBox(1, 0, 0, 200, 10);
  ExpectBox(2, 200, 0, 100, 10);
}

TEST_F(LayoutTest, MaxWidthFreezesAndRedistributes) {
  Add(kNoNode);
  Style a{};
  a.basis = L::Px(0);
  a.grow = 1;
  a.max_width = L::Px(50);
  Style b = a;
  b.max_width = L::Auto();
  Add(0, a);
  Add(0, b);
  Run(300, 50);
  ExpectBox(1, 0, 0, 50, 50);
  ExpectBox(2, 50, 0, 250, 50);
}

TEST_F(LayoutTest, MinWidthFreezesWhileShrinking) {
  Add(kNoNode);
  Style a = Fixed(200, 10);
  a.min_width = L::Px(180);
  Add(0, a);
  Add(0, Fixed(200, 10));
  Run(300, 50);
  // equal shrink gives 150/150; a is clamped to 180, so b takes the rest: 120
  ExpectBox(1, 0, 0, 180, 10);
  ExpectBox(2, 180, 0, 120, 10);
}

TEST_F(LayoutTest, FractionalGrowDoesNotConsumeAllSpace) {
  Add(kNoNode);
  Style a{};
  a.basis = L::Px(0);
  a.grow = 0.25f;
  Add(0, a);
  Run(400, 50);
  ExpectBox(1, 0, 0, 100, 50);  // 0.25 of 400
}

struct AlignCase {
  Align align;
  Scalar y;
  Scalar h;
};

class AlignTest : public LayoutTest, public ::testing::WithParamInterface<AlignCase> {};

TEST_P(AlignTest, Cross) {
  Style root{};
  root.align = GetParam().align;
  Add(kNoNode, root);
  Style item{};
  item.width = L::Px(50);
  if (GetParam().align != Align::kStretch)
    item.height = L::Px(20);
  Add(0, item);
  Run(400, 100);
  ExpectBox(1, 0, GetParam().y, 50, GetParam().h);
}

INSTANTIATE_TEST_SUITE_P(All, AlignTest,
                         ::testing::Values(AlignCase{Align::kStart, 0, 20}, AlignCase{Align::kCenter, 40, 20},
                                           AlignCase{Align::kEnd, 80, 20}, AlignCase{Align::kStretch, 0, 100}));

TEST_F(LayoutTest, AlignSelfOverrides) {
  Style root{};
  root.align = Align::kStart;
  Add(kNoNode, root);
  Style a = Fixed(50, 20);
  a.align_self = Align::kEnd;
  Add(0, a);
  Add(0, Fixed(50, 20));
  Run(400, 100);
  ExpectBox(1, 0, 80, 50, 20);
  ExpectBox(2, 50, 0, 50, 20);
}

TEST_F(LayoutTest, StretchRespectsMaxHeight) {
  Add(kNoNode);
  Style a{};
  a.width = L::Px(50);
  a.max_height = L::Px(40);
  Add(0, a);
  Run(400, 100);
  ExpectBox(1, 0, 0, 50, 40);
}

TEST_F(LayoutTest, ColumnDirection) {
  Style root{};
  root.direction = Direction::kColumn;
  root.align = Align::kStart;
  root.gap = L::Px(10);
  Add(kNoNode, root);
  Add(0, Fixed(50, 20));
  Add(0, Fixed(60, 30));
  Run(400, 200);
  ExpectBox(1, 0, 0, 50, 20);
  ExpectBox(2, 0, 30, 60, 30);
}

TEST_F(LayoutTest, ColumnGrowAndStretch) {
  Style root{};
  root.direction = Direction::kColumn;
  Add(kNoNode, root);
  Style a{};
  a.height = L::Px(50);
  Style b{};
  b.grow = 1;
  Add(0, a);
  Add(0, b);
  Run(300, 200);
  ExpectBox(1, 0, 0, 300, 50);
  ExpectBox(2, 0, 50, 300, 150);
}

TEST_F(LayoutTest, RowReverse) {
  Style root{};
  root.direction = Direction::kRowReverse;
  root.align = Align::kStart;
  Add(kNoNode, root);
  Add(0, Fixed(50, 20));
  Add(0, Fixed(60, 20));
  Run(300, 100);
  ExpectBox(1, 250, 0, 50, 20);
  ExpectBox(2, 190, 0, 60, 20);
}

TEST_F(LayoutTest, RowReverseJustifyEndPacksLeft) {
  Style root{};
  root.direction = Direction::kRowReverse;
  root.justify = Justify::kEnd;
  root.align = Align::kStart;
  Add(kNoNode, root);
  Add(0, Fixed(50, 20));
  Add(0, Fixed(60, 20));
  Run(300, 100);
  ExpectBox(1, 60, 0, 50, 20);
  ExpectBox(2, 0, 0, 60, 20);
}

TEST_F(LayoutTest, ColumnReverse) {
  Style root{};
  root.direction = Direction::kColumnReverse;
  root.align = Align::kStart;
  Add(kNoNode, root);
  Add(0, Fixed(50, 20));
  Add(0, Fixed(50, 30));
  Run(300, 100);
  ExpectBox(1, 0, 80, 50, 20);
  ExpectBox(2, 0, 50, 50, 30);
}

TEST_F(LayoutTest, WrapBreaksLines) {
  Style root{};
  root.wrap = true;
  root.gap = L::Px(10);
  root.align = Align::kStart;
  root.height = L::Auto();
  Add(kNoNode, root);
  for (auto i = 0; i < 5; ++i)
    Add(0, Fixed(100, 30));
  // a column parent would shrink-wrap the height; here the root fills, so check positions only
  Run(250, 400);
  // 100 + 10 + 100 = 210 fits, 320 does not: 2, 2, 1. Lines stretch to share 400 - (90 + 20) = 290.
  const auto line = 30.0f + 290.0f / 3;
  ExpectBox(1, 0, 0, 100, 30);
  ExpectBox(2, 110, 0, 100, 30);
  ExpectBox(3, 0, line + 10, 100, 30);
  ExpectBox(4, 110, line + 10, 100, 30);
  ExpectBox(5, 0, 2 * (line + 10), 100, 30);
}

TEST_F(LayoutTest, WrapAutoHeightIsSumOfLines) {
  // wrap container inside a column: its height is the lines' sum
  Style root{};
  root.direction = Direction::kColumn;
  root.align = Align::kStart;
  Add(kNoNode, root);
  Style w{};
  w.wrap = true;
  w.gap = L::Px(10);
  w.width = L::Px(250);
  w.align = Align::kStart;
  const auto wrapper = Add(0, w);
  for (auto i = 0; i < 5; ++i)
    Add(wrapper, Fixed(100, 30));
  Run(400, 400);
  ExpectBox(wrapper, 0, 0, 250, 3 * 30 + 2 * 10);
  ExpectBox(6, 0, 80, 100, 30);
}

TEST_F(LayoutTest, PaddingIsBorderBox) {
  Style root{};
  root.padding = Edges::All(L::Px(10));
  root.align = Align::kStart;
  Add(kNoNode, root);
  Style item{};
  item.width = L::Pct(50);
  item.height = L::Px(20);
  Add(0, item);
  Run(200, 100);
  // inner width 180 -> 90
  ExpectBox(1, 10, 10, 90, 20);
}

TEST_F(LayoutTest, ShrinkWrapNestedContainer) {
  Style root{};
  root.direction = Direction::kColumn;
  root.align = Align::kStart;
  Add(kNoNode, root);
  Style row{};
  row.gap = L::Px(10);
  row.padding = Edges::All(L::Px(5));
  row.align = Align::kStart;
  const auto c = Add(0, row);
  Add(c, Fixed(40, 20));
  Add(c, Fixed(40, 30));
  Run(400, 400);
  // 5 + 40 + 10 + 40 + 5 = 100 wide; 5 + 30 + 5 = 40 tall
  ExpectBox(c, 0, 0, 100, 40);
  ExpectBox(2, 5, 5, 40, 20);
  ExpectBox(3, 55, 5, 40, 30);
}

TEST_F(LayoutTest, NestedStretch) {
  Style root{};
  root.direction = Direction::kColumn;
  Add(kNoNode, root);
  Style inner{};
  inner.grow = 1;
  const auto c = Add(0, inner);
  Style leaf{};
  leaf.grow = 1;
  Add(c, leaf);
  Run(300, 200);
  ExpectBox(c, 0, 0, 300, 200);
  ExpectBox(2, 0, 0, 300, 200);
}

TEST_F(LayoutTest, PercentGap) {
  Style root{};
  root.gap = L::Pct(10);
  root.align = Align::kStart;
  Add(kNoNode, root);
  Add(0, Fixed(50, 20));
  Add(0, Fixed(50, 20));
  Run(400, 100);
  ExpectBox(2, 90, 0, 50, 20);
}

TEST_F(LayoutTest, EmptyContainerSizesToPadding) {
  Style root{};
  root.direction = Direction::kColumn;
  root.align = Align::kStart;
  Add(kNoNode, root);
  Style c{};
  c.padding = Edges::All(L::Px(7));
  const auto child = Add(0, c);
  Add(child, Style{});
  Run(100, 100);
  ExpectBox(child, 0, 0, 14, 14);
}

// A text node 100 wide and 20 tall that wraps to ceil(100 / width) lines.
static auto MeasureText(void*, NodeId, const Scalar max_width) -> Size {
  if (!IsDefined(max_width) || max_width >= 100)
    return Size{100, 20};
  const auto lines = std::ceil(100 / std::max(max_width, 1.0f));
  return Size{max_width, 20 * lines};
}

TEST_F(LayoutTest, MeasuredLeafNaturalSize) {
  Style root{};
  root.align = Align::kStart;
  Add(kNoNode, root);
  Add(0, Style{}, true);
  Run(400, 100, MeasureText);
  ExpectBox(1, 0, 0, 100, 20);
}

TEST_F(LayoutTest, MeasuredLeafWrapsWhenShrunk) {
  Style root{};
  root.direction = Direction::kColumn;
  root.align = Align::kStart;
  Add(kNoNode, root);
  Style row{};
  row.width = L::Px(60);
  row.align = Align::kStart;
  const auto c = Add(0, row);
  Add(c, Style{}, true);
  Run(400, 400, MeasureText);
  // the text shrinks to 60 and so wraps to two lines; the container is as tall as the text
  ExpectBox(c, 0, 0, 60, 40);
  ExpectBox(2, 0, 0, 60, 40);
}

TEST_F(LayoutTest, ButtonSizesToLabelPlusPadding) {
  Style root{};
  root.direction = Direction::kColumn;
  root.align = Align::kStart;
  Add(kNoNode, root);
  Style button{};
  button.padding = Edges::All(L::Px(8));
  const auto b = Add(0, button);
  Add(b, Style{}, true);
  Run(400, 400, MeasureText);
  ExpectBox(b, 0, 0, 116, 36);
  ExpectBox(2, 8, 8, 100, 20);
}

TEST_F(LayoutTest, ContentsChildrenJoinGrandparent) {
  Style root{};
  root.align = Align::kStart;
  Add(kNoNode, root);
  Add(0, Fixed(10, 10));
  Style fragment{};
  fragment.display = Display::kContents;
  const auto f = Add(0, fragment);
  Add(f, Fixed(20, 10));
  Add(f, Fixed(30, 10));
  Add(0, Fixed(40, 10));
  Run(400, 100);
  ExpectBox(1, 0, 0, 10, 10);
  ExpectBox(3, 10, 0, 20, 10);
  ExpectBox(4, 30, 0, 30, 10);
  ExpectBox(5, 60, 0, 40, 10);
  ExpectBox(f, 10, 0, 50, 10);  // the union of its children
}

TEST_F(LayoutTest, EmptyContentsTakesNoSpace) {
  Style root{};
  root.align = Align::kStart;
  Add(kNoNode, root);
  Style fragment{};
  fragment.display = Display::kContents;
  const auto f = Add(0, fragment);
  Add(0, Fixed(40, 10));
  Run(400, 100);
  ExpectBox(f, 0, 0, 0, 0);
  ExpectBox(2, 0, 0, 40, 10);
}

TEST_F(LayoutTest, ViewportChangeReflows) {
  Add(kNoNode);
  Style a{};
  a.grow = 1;
  a.basis = L::Px(0);
  Add(0, a);
  Engine engine{};
  boxes.assign(nodes.size(), Box{});
  engine.Run(nodes, 0, Size{300, 100}, boxes);
  ExpectBox(1, 0, 0, 300, 100);
  engine.Run(nodes, 0, Size{500, 80}, boxes);
  ExpectBox(1, 0, 0, 500, 80);
}

TEST_F(LayoutTest, RerunIsDeterministic) {
  Add(kNoNode);
  Style a{};
  a.grow = 1;
  const auto c = Add(0, a);
  Add(c, Style{}, true);
  Engine engine{};
  boxes.assign(nodes.size(), Box{});
  engine.Run(nodes, 0, Size{300, 100}, boxes, MeasureText);
  const auto first = boxes;
  engine.Run(nodes, 0, Size{300, 100}, boxes, MeasureText);
  for (size_t i = 0; i < boxes.size(); ++i) {
    EXPECT_EQ(first[i].x, boxes[i].x);
    EXPECT_EQ(first[i].width, boxes[i].width);
  }
}

// Auto-sized containers are measured before they are laid out, at every level. Without the cache that is
// exponential in the depth.
TEST_F(LayoutTest, DeepAutoSizedNestingIsNotExponential) {
  Style root{};
  root.direction = Direction::kColumn;
  root.align = Align::kStart;
  Add(kNoNode, root);
  auto parent = NodeId{0};
  for (auto depth = 0; depth < 40; ++depth) {
    Style s{};
    s.direction = (depth % 2) ? Direction::kRow : Direction::kColumn;
    s.align = Align::kStart;
    s.padding = Edges::All(L::Px(1));
    parent = Add(parent, s);
    Add(parent, Fixed(10, 10));
  }
  boxes.assign(nodes.size(), Box{});
  Engine engine{};
  const auto start = std::chrono::steady_clock::now();
  engine.Run(nodes, 0, Size{1000, 1000}, boxes);
  const auto elapsed = std::chrono::steady_clock::now() - start;
  EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 500);
  EXPECT_LT(engine.GetNumberOfComputes(), nodes.size() * 40);
}

TEST_F(LayoutTest, WideTreeScales) {
  Style root{};
  root.wrap = true;
  Add(kNoNode, root);
  for (auto i = 0; i < 3000; ++i) {
    const auto c = Add(0, Fixed(20, 20));
    (void)c;
  }
  boxes.assign(nodes.size(), Box{});
  Engine engine{};
  engine.Run(nodes, 0, Size{1000, 1200}, boxes);
  // 50 per row
  EXPECT_NEAR(boxes[51].x, 0, 1e-3);
  EXPECT_NEAR(boxes[51].y, boxes[1].y + 20, 1.0);
}
}  // namespace
}  // namespace kura::layout
