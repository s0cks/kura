#include <gtest/gtest.h>

#include "backend/compile_type.h"
#include "dom/kind.h"
#include "dom/property.h"
#include "object.h"

using namespace kura;
using namespace kura::dom;

TEST(DomKind, TagsRoundTrip) {
#define CHECK_TAG(Name, Tag)                      \
  EXPECT_EQ(FindKind(Tag), Kind::k##Name) << Tag; \
  EXPECT_EQ(KindTag(Kind::k##Name), Tag);
  FOR_EACH_ELEMENT_NODE(CHECK_TAG)
#undef CHECK_TAG
}

TEST(DomKind, DocumentIsGone) {
  EXPECT_FALSE(FindKind("document").has_value());
  EXPECT_FALSE(FindKind("dynamic").has_value());
}

TEST(DomKind, FragmentReservesNoSpace) {
  EXPECT_FALSE(ReservesSpace(Kind::kFragment));
  EXPECT_TRUE(ReservesSpace(Kind::kBlock));
  EXPECT_FALSE(ReservesSpace(Kind::kDynamic));
  EXPECT_TRUE(CanHaveChildren(Kind::kFragment));
  EXPECT_FALSE(CanHaveChildren(Kind::kText));
}

TEST(DomProperty, AppliesToTheRightKinds) {
  EXPECT_NE(FindProperty(Kind::kButton, "width"), nullptr);
  EXPECT_NE(FindProperty(Kind::kText, "color"), nullptr);
  EXPECT_EQ(FindProperty(Kind::kButton, "color"), nullptr);
  EXPECT_EQ(FindProperty(Kind::kFragment, "width"), nullptr);
  EXPECT_EQ(FindProperty(Kind::kText, "placeholder"), nullptr);
  EXPECT_NE(FindProperty(Kind::kInput, "placeholder"), nullptr);
}

TEST(DomProperty, ValueDependsOnTheKind) {
  const auto text = FindProperty(Kind::kText, "value");
  const auto input = FindProperty(Kind::kInput, "value");
  const auto progress = FindProperty(Kind::kProgress, "value");
  ASSERT_NE(text, nullptr);
  ASSERT_NE(input, nullptr);
  ASSERT_NE(progress, nullptr);
  EXPECT_TRUE(text->Accepts(CompileType::kString));
  EXPECT_FALSE(text->Accepts(CompileType::kInt));
  EXPECT_FALSE(input->Accepts(CompileType::kInt));
  EXPECT_TRUE(progress->Accepts(CompileType::kFloat));
  EXPECT_FALSE(progress->Accepts(CompileType::kString));
}

TEST(DomProperty, EventsAreMarked) {
  EXPECT_TRUE(FindProperty(Kind::kButton, "on-click")->event);
  EXPECT_FALSE(FindProperty(Kind::kButton, "fill")->event);
}

TEST(DomProperty, Suggestions) {
  const auto suggestion = SuggestProperty(Kind::kButton, "widht");
  ASSERT_NE(suggestion, nullptr);
  EXPECT_EQ(suggestion->name, "width");
  EXPECT_EQ(SuggestProperty(Kind::kButton, "completely-different"), nullptr);
}

TEST(DomProperty, DescribeAccepted) {
  EXPECT_EQ(DescribeAccepted(*FindProperty(Kind::kBlock, "radius")), "Px");
  EXPECT_EQ(DescribeAccepted(*FindProperty(Kind::kBlock, "width")), "Px, Pct, Fr or \"auto\"");
  EXPECT_EQ(DescribeAccepted(*FindProperty(Kind::kBlock, "wrap")), "Bool");
}

TEST(DomCheckConstant, Types) {
  const auto width = FindProperty(Kind::kBlock, "width");
  EXPECT_FALSE(CheckConstant(*width, Px::New(10)));
  EXPECT_FALSE(CheckConstant(*width, Pct::New(50)));
  EXPECT_FALSE(CheckConstant(*width, Fr::New(1)));
  EXPECT_FALSE(CheckConstant(*width, String::New("auto")));
  EXPECT_TRUE(CheckConstant(*width, Int::New(10)));
  EXPECT_TRUE(CheckConstant(*width, Float::New(10)));
  EXPECT_TRUE(CheckConstant(*width, None::Get()));

  const auto fill = FindProperty(Kind::kBlock, "fill");
  EXPECT_FALSE(CheckConstant(*fill, Color::New(0, 0, 0, 0)));
  EXPECT_TRUE(CheckConstant(*fill, Int::New(0)));
}

TEST(DomCheckConstant, Words) {
  const auto direction = FindProperty(Kind::kBlock, "direction");
  EXPECT_FALSE(CheckConstant(*direction, String::New("row")));
  const auto problem = CheckConstant(*direction, String::New("diagonal"));
  ASSERT_TRUE(problem);
  EXPECT_NE(problem->find("\"diagonal\" isn't one of"), std::string::npos);
  // only a width or a height can be "auto"
  EXPECT_TRUE(CheckConstant(*FindProperty(Kind::kBlock, "padding"), String::New("auto")));
}

TEST(DomCheckConstant, Ranges) {
  const auto radius = FindProperty(Kind::kBlock, "radius");
  EXPECT_FALSE(CheckConstant(*radius, Px::New(0)));
  const auto negative = CheckConstant(*radius, Px::New(-1));
  ASSERT_TRUE(negative);
  EXPECT_NE(negative->find("can't be negative"), std::string::npos);

  const auto opacity = FindProperty(Kind::kBlock, "opacity");
  EXPECT_FALSE(CheckConstant(*opacity, Float::New(0.5)));
  EXPECT_FALSE(CheckConstant(*opacity, Int::New(1)));
  const auto too_much = CheckConstant(*opacity, Float::New(2));
  ASSERT_TRUE(too_much);
  EXPECT_NE(too_much->find("between 0 and 1"), std::string::npos);

  // a margin can be negative
  EXPECT_FALSE(CheckConstant(*FindProperty(Kind::kBlock, "margin"), Px::New(-4)));
}

TEST(DomCheckConstant, EventsNeedAMessage) {
  const auto click = FindProperty(Kind::kButton, "on-click");
  EXPECT_TRUE(CheckConstant(*click, Int::New(1)));
  EXPECT_TRUE(CheckConstant(*click, String::New("tick")));
}

TEST(DomCheckType, Static) {
  const auto width = FindProperty(Kind::kBlock, "width");
  EXPECT_FALSE(CheckType(*width, CompileType::Px()));
  EXPECT_FALSE(CheckType(*width, CompileType::Any()));
  EXPECT_FALSE(CheckType(*width, CompileType::Never()));
  EXPECT_TRUE(CheckType(*width, CompileType::Int()));
  EXPECT_TRUE(CheckType(*width, CompileType::None()));
  EXPECT_FALSE(CheckType(*width, CompileType::Union({CompileType::Px(), CompileType::Pct()})));
  EXPECT_TRUE(CheckType(*width, CompileType::Union({CompileType::Px(), CompileType::None()})));
}

TEST(DomCheckType, Messages) {
  const auto click = FindProperty(Kind::kButton, "on-click");
  EXPECT_FALSE(CheckType(*click, CompileType::Variant("Tick")));
  EXPECT_FALSE(CheckType(*click, CompileType::Union({CompileType::Variant("Tick"), CompileType::Variant("Tock")})));
  EXPECT_TRUE(CheckType(*click, CompileType::Int()));
}
