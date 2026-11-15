#include <gtest/gtest.h>

#include <cstring>

#include "geometry.h"
#include "renderer/renderer_abi.h"

namespace {
using kura::geom::Rect;
using kura::geom::Vec2;
using kura::geom::Vec3;

TEST(Vec2Test, StartsAtTheOrigin) {
  constexpr Vec2 v{};
  EXPECT_EQ(v.x, 0);
  EXPECT_EQ(v.y, 0);
}

TEST(Vec2Test, Arithmetic) {
  constexpr Vec2 a{1, 2};
  constexpr Vec2 b{3, 5};
  EXPECT_EQ(a + b, Vec2(4, 7));
  EXPECT_EQ(b - a, Vec2(2, 3));
  EXPECT_EQ(a * 3, Vec2(3, 6));
  EXPECT_FLOAT_EQ(a.Dot(b), 13);
  EXPECT_FLOAT_EQ(Vec2(3, 4).Length(), 5);
}

TEST(Vec3Test, Arithmetic) {
  constexpr Vec3 a{1, 2, 3};
  constexpr Vec3 b{4, 5, 6};
  EXPECT_EQ(a + b, Vec3(5, 7, 9));
  EXPECT_EQ(b - a, Vec3(3, 3, 3));
  EXPECT_EQ(a * 2, Vec3(2, 4, 6));
  EXPECT_FLOAT_EQ(a.Dot(b), 32);
  EXPECT_FLOAT_EQ(Vec3(2, 3, 6).Length(), 7);
}

TEST(Vec3Test, TheCrossProductIsPerpendicularToBoth) {
  constexpr Vec3 x{1, 0, 0};
  constexpr Vec3 y{0, 1, 0};
  EXPECT_EQ(x.Cross(y), Vec3(0, 0, 1));
  EXPECT_EQ(y.Cross(x), Vec3(0, 0, -1));
  const Vec3 a{1, 2, 3};
  const Vec3 b{-2, 0.5f, 4};
  const auto c = a.Cross(b);
  EXPECT_NEAR(c.Dot(a), 0, 1e-5);
  EXPECT_NEAR(c.Dot(b), 0, 1e-5);
}

TEST(RectTest, EdgesAndCorners) {
  constexpr Rect r{10, 20, 30, 40};
  EXPECT_EQ(r.Origin(), Vec2(10, 20));
  EXPECT_EQ(r.Size(), Vec2(30, 40));
  EXPECT_FLOAT_EQ(r.Right(), 40);
  EXPECT_FLOAT_EQ(r.Bottom(), 60);
  EXPECT_EQ(r.Center(), Vec2(25, 40));
  EXPECT_EQ(Rect(Vec2(10, 20), Vec2(30, 40)), r);
}

TEST(RectTest, TheLeftAndTopEdgesAreInAndTheRightAndBottomAreNot) {
  constexpr Rect r{0, 0, 10, 10};
  EXPECT_TRUE(r.Contains({0, 0}));
  EXPECT_TRUE(r.Contains({9.99f, 9.99f}));
  EXPECT_FALSE(r.Contains({10, 5}));
  EXPECT_FALSE(r.Contains({5, 10}));
  EXPECT_FALSE(r.Contains({-0.01f, 5}));
  // a rectangle that has no area holds nothing
  EXPECT_FALSE(Rect(5, 5, 0, 10).Contains({5, 6}));
  EXPECT_TRUE(Rect(5, 5, 0, 10).IsEmpty());
  EXPECT_FALSE(r.IsEmpty());
}

TEST(RectTest, RectanglesThatShareAnEdgeDontShareAPoint) {
  constexpr Rect left{0, 0, 10, 10};
  constexpr Rect right{10, 0, 10, 10};
  for (float x = 0; x < 20; x += 0.5f) {
    const Vec2 p{x, 5};
    EXPECT_NE(left.Contains(p), right.Contains(p)) << x;
  }
}

TEST(RectTest, TheIntersectionIsWhatIsInBoth) {
  constexpr Rect a{0, 0, 10, 10};
  constexpr Rect b{5, 6, 10, 10};
  EXPECT_EQ(a.Intersect(b), Rect(5, 6, 5, 4));
  EXPECT_EQ(b.Intersect(a), Rect(5, 6, 5, 4));
  EXPECT_EQ(a.Intersect(a), a);
}

TEST(RectTest, RectanglesThatDontMeetIntersectInNothing) {
  constexpr Rect a{0, 0, 10, 10};
  const auto apart = a.Intersect(Rect(20, 20, 5, 5));
  EXPECT_TRUE(apart.IsEmpty());
  EXPECT_GE(apart.width, 0) << "never a negative size";
  EXPECT_GE(apart.height, 0);
  EXPECT_TRUE(a.Intersect(Rect(10, 0, 5, 5)).IsEmpty()) << "touching is not meeting";
}

TEST(RectTest, InsetMovesEveryEdgeIn) {
  constexpr Rect r{0, 0, 10, 20};
  EXPECT_EQ(r.Inset(2), Rect(2, 2, 6, 16));
  EXPECT_EQ(r.Inset(-1), Rect(-1, -1, 12, 22));
  EXPECT_EQ(r.Inset(0), r);
}

TEST(RectTest, InsetMoreThanItIsLargeIsEmptyAtTheMiddle) {
  constexpr Rect r{0, 0, 10, 4};
  const auto gone = r.Inset(3);
  EXPECT_TRUE(gone.IsEmpty());
  EXPECT_FLOAT_EQ(gone.height, 0);
  EXPECT_FLOAT_EQ(gone.y, 2) << "the middle of the height";
}

// What makes them worth sharing: they are the renderer's, bit for bit.
TEST(AbiTest, ARectIsAKuraRect) {
  const Rect rect{1.5f, 2.5f, 3.5f, 4.5f};
  KuraRect abi{};
  std::memcpy(&abi, &rect, sizeof(abi));
  EXPECT_EQ(abi.x, 1.5f);
  EXPECT_EQ(abi.y, 2.5f);
  EXPECT_EQ(abi.width, 3.5f);
  EXPECT_EQ(abi.height, 4.5f);
}

TEST(AbiTest, TheVectorsAreTheRenderers) {
  const Vec2 v2{1, 2};
  KuraVec2 abi2{};
  std::memcpy(&abi2, &v2, sizeof(abi2));
  EXPECT_EQ(abi2.x, 1);
  EXPECT_EQ(abi2.y, 2);

  const Vec3 v3{1, 2, 3};
  KuraVec3 abi3{};
  std::memcpy(&abi3, &v3, sizeof(abi3));
  EXPECT_EQ(abi3.x, 1);
  EXPECT_EQ(abi3.y, 2);
  EXPECT_EQ(abi3.z, 3);
}

TEST(ToStringTest, ShowsTheFields) {
  EXPECT_EQ(Vec2(1, 2).ToString().substr(0, 5), "Vec2(");
  EXPECT_EQ(Vec3(1, 2, 3).ToString().substr(0, 5), "Vec3(");
  EXPECT_EQ(Rect(1, 2, 3, 4).ToString().substr(0, 5), "Rect(");
}
}  // namespace
