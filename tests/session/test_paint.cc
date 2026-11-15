// What `fill`, `border-width`, `border-color`, `radius` and `opacity` make of a box, and `opacity` and `text-align`
// of a text, as the draw commands of a frame. tests/session/programs/paint.kui has one of each, in a column.
#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "backend/program.h"
#include "backend/session.h"
#include "renderer/headless/renderer.h"

namespace {
using kura::Session;
using kura::renderer::Command;
using kura::renderer::HeadlessOptions;
using kura::renderer::HeadlessRenderer;

class PaintTest : public ::testing::Test {
 protected:
  void SetUp() override {
    renderer_ = std::make_unique<HeadlessRenderer>(HeadlessOptions{.rasterize = false});
    abi_ = renderer_->Abi();
    ASSERT_EQ(kura_rt_set_renderer(&abi_), KURA_RENDERER_OK);
    kura_rt_set_viewport(300, 400);
    std::string error{};
    program_ = kura::Program::Load(std::string(KURA_SESSION_PROGRAMS_DIR) + "/paint.kui", {}, &error);
    ASSERT_NE(program_, nullptr) << error;
    session_ = Session::Create(program_.get(), &error);
    ASSERT_NE(session_, nullptr) << error;
    ASSERT_TRUE(session_->Render(&error)) << error;
  }

  void TearDown() override {
    kura_rt_events_clear();
    kura_rt_set_renderer(nullptr);
  }

  // The fills inside the rows from `top` down to `bottom`: what one of the boxes made.
  auto FillsBetween(const float top, const float bottom) const -> std::vector<Command> {
    std::vector<Command> found{};
    for (const auto& command : renderer_->GetCommands()) {
      if (command.kind == Command::Kind::kFillRect && command.rect.y >= top && command.rect.y < bottom)
        found.push_back(command);
    }
    return found;
  }

  auto TextOf(const std::string& label) const -> Command {
    for (const auto& command : renderer_->GetCommands()) {
      if (command.kind == Command::Kind::kText && command.text == label)
        return command;
    }
    ADD_FAILURE() << "no text " << label;
    return {};
  }

  std::unique_ptr<HeadlessRenderer> renderer_;
  KuraRenderer abi_{};
  std::unique_ptr<kura::Program> program_;
  std::unique_ptr<Session> session_;
};

// The boxes are 10 tall with 4 between them: the n-th starts at 14 * n.
TEST_F(PaintTest, ARoundedBoxIsMissingItsCorners) {
  auto fills = FillsBetween(0, 10);
  fills.erase(fills.begin());  // the column's own, the first thing drawn
  ASSERT_GT(fills.size(), 3u);
  EXPECT_GT(fills.front().rect.x, 1) << "the top row starts inside the box: the corner is cut";
  EXPECT_LT(fills.front().rect.width, 20);
  const auto middle = std::find_if(fills.begin(), fills.end(), [](const Command& c) { return c.rect.x == 0; });
  ASSERT_NE(middle, fills.end());
  EXPECT_FLOAT_EQ(middle->rect.width, 20) << "and the middle is the whole width";
  EXPECT_NEAR(fills.back().rect.x, fills.front().rect.x, 0.001) << "the bottom is the same as the top";
}

TEST_F(PaintTest, ABorderIsAFrameInsideTheEdge) {
  const auto fills = FillsBetween(14, 24);
  // the fill, then the border: the top and the bottom, and a side each
  ASSERT_EQ(fills.size(), 5u);
  EXPECT_FLOAT_EQ(fills[0].rect.width, 20);
  EXPECT_FLOAT_EQ(fills[0].rect.height, 10);
  EXPECT_GT(fills[1].color.b, fills[1].color.r) << "the border is blue";
  EXPECT_FLOAT_EQ(fills[1].rect.height, 2);
  EXPECT_FLOAT_EQ(fills[2].rect.width, 2);
  EXPECT_FLOAT_EQ(fills[2].rect.x, 0);
  EXPECT_FLOAT_EQ(fills[3].rect.width, 2);
  EXPECT_FLOAT_EQ(fills[3].rect.x, 18);
  EXPECT_FLOAT_EQ(fills[4].rect.height, 2);
  EXPECT_FLOAT_EQ(fills[4].rect.y, 22);
}

TEST_F(PaintTest, OpacityLightensTheFill) {
  const auto fills = FillsBetween(28, 38);
  ASSERT_EQ(fills.size(), 1u);
  EXPECT_NEAR(fills[0].color.a, 0.5f, 0.01);
  EXPECT_GT(fills[0].color.g, fills[0].color.r);
}

TEST_F(PaintTest, ABorderColorAloneIsOnePixel) {
  const auto fills = FillsBetween(42, 52);
  ASSERT_EQ(fills.size(), 4u) << "no fill, so only the frame";
  EXPECT_FLOAT_EQ(fills[0].rect.height, 1);
  EXPECT_FLOAT_EQ(fills[1].rect.width, 1);
  EXPECT_GT(fills[0].color.r, fills[0].color.b) << "red";
}

TEST_F(PaintTest, ABorderWidthAloneIsBlack) {
  const auto fills = FillsBetween(56, 66);
  ASSERT_EQ(fills.size(), 4u);
  EXPECT_FLOAT_EQ(fills[0].rect.height, 3);
  EXPECT_FLOAT_EQ(fills[0].color.r, 0);
  EXPECT_FLOAT_EQ(fills[0].color.g, 0);
  EXPECT_FLOAT_EQ(fills[0].color.b, 0);
  EXPECT_FLOAT_EQ(fills[0].color.a, 1);
}

TEST_F(PaintTest, NothingIsDrawnWithNoOpacity) {
  EXPECT_TRUE(FillsBetween(70, 80).empty());
}

TEST_F(PaintTest, OpacityLightensText) {
  EXPECT_NEAR(TextOf("faded").color.a, 0.5f, 0.01);
  EXPECT_FLOAT_EQ(TextOf("start").color.a, 1);
}

TEST_F(PaintTest, TextAlignPlacesTheLinesInTheTextsRoom) {
  // the texts are as wide as the column, 100, because it stretches them
  const auto start = TextOf("start");
  const auto middle = TextOf("middle");
  const auto end = TextOf("end");
  EXPECT_FLOAT_EQ(start.from.x, 0);
  EXPECT_GT(middle.from.x, 10);
  EXPECT_GT(end.from.x, middle.from.x);
  EXPECT_GT(end.from.x, 50) << "a short text at the end of 100 is well over half way";
}

TEST_F(PaintTest, AScrollBoxIsRoundedToo) {
  // the scroll container paints its own box before it clips what is in it
  bool clipped = false;
  bool rounded = false;
  for (const auto& command : renderer_->GetCommands()) {
    if (command.kind == Command::Kind::kPushClip)
      clipped = true;
    if (command.kind == Command::Kind::kFillRect && !clipped && command.rect.y > 100 && command.rect.x > 0)
      rounded = true;
  }
  EXPECT_TRUE(clipped);
  EXPECT_TRUE(rounded);
}
}  // namespace
