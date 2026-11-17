// Tooltips: a node's `tooltip` shows next to the pointer once the pointer has been over it for the delay, over
// everything else in the frame. tests/session/programs/tooltip.kui is a column of nodes with tooltips, at the heights its
// comment says.
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

constexpr uint32_t kWidth = 400;
constexpr uint32_t kHeight = 300;

struct Box {
  float left = 0;
  float top = 0;
  float right = 0;
  float bottom = 0;
  bool found = false;

  auto width() const -> float {
    return right - left;
  }
};

class TooltipTest : public ::testing::Test {
 protected:
  void SetUp() override {
    Start(kWidth, kHeight);
  }

  void Start(const uint32_t width, const uint32_t height) {
    session_.reset();
    program_.reset();
    renderer_ = std::make_unique<HeadlessRenderer>(HeadlessOptions{.rasterize = false});
    abi_ = renderer_->Abi();
    ASSERT_EQ(kura_rt_set_renderer(&abi_), KURA_RENDERER_OK);
    kura_rt_set_viewport(width, height);
    kura_rt_pointer_left();
    default_style_ = kura_rt_tooltip_default_style();
    std::string error{};
    program_ = kura::Program::Load(std::string(KURA_SESSION_PROGRAMS_DIR) + "/tooltip.kui", {}, &error);
    ASSERT_NE(program_, nullptr) << error;
    session_ = Session::Create(program_.get(), &error);
    ASSERT_NE(session_, nullptr) << error;
    session_->SetTooltipStyle(default_style_);
  }

  void TearDown() override {
    session_->PointerLeft();
    kura_rt_events_clear();
    kura_rt_set_renderer(nullptr);
  }

  // A frame at `time`, with the pointer at (x, y).
  void Frame(const double time, const double x, const double y) {
    session_->SetTime(time);
    session_->PointerMoved(x, y);
    std::string error{};
    ASSERT_TRUE(session_->Render(&error)) << error;
  }

  auto Text(const std::string& label) const -> const Command* {
    for (const auto& command : renderer_->GetCommands()) {
      if (command.kind == Command::Kind::kText && command.text == label)
        return &command;
    }
    return nullptr;
  }

  auto Shown(const std::string& label) const -> bool {
    return Text(label) != nullptr;
  }

  // What is drawn in the tooltip's background: the fills that have its color.
  auto TooltipBox() const -> Box {
    Box box{};
    for (const auto& command : renderer_->GetCommands()) {
      if (command.kind != Command::Kind::kFillRect || command.color.a < 0.9f || command.color.a > 0.99f ||
          command.color.r > 0.2f)
        continue;
      const auto& r = command.rect;
      if (!box.found) {
        box = Box{r.x, r.y, r.x + r.width, r.y + r.height, true};
      } else {
        box.left = std::min(box.left, r.x);
        box.top = std::min(box.top, r.y);
        box.right = std::max(box.right, r.x + r.width);
        box.bottom = std::max(box.bottom, r.y + r.height);
      }
    }
    return box;
  }

  auto Texts() const -> std::vector<Command> {
    std::vector<Command> texts{};
    for (const auto& command : renderer_->GetCommands()) {
      if (command.kind == Command::Kind::kText)
        texts.push_back(command);
    }
    return texts;
  }

  std::unique_ptr<HeadlessRenderer> renderer_;
  KuraRenderer abi_{};
  KuraTooltipStyle default_style_{};
  std::unique_ptr<kura::Program> program_;
  std::unique_ptr<Session> session_;
};

TEST_F(TooltipTest, NothingShowsBeforeTheDelayIsOver) {
  Frame(0.0, 50, 15);
  EXPECT_FALSE(Shown("Save the file"));
  EXPECT_TRUE(session_->TooltipPending());
  Frame(0.3, 50, 15);
  EXPECT_FALSE(Shown("Save the file"));
  EXPECT_TRUE(session_->TooltipPending());
}

TEST_F(TooltipTest, ItShowsOnceThePointerHasBeenThereForTheDelay) {
  Frame(0.0, 50, 15);
  Frame(0.6, 50, 15);
  EXPECT_TRUE(Shown("Save the file"));
  EXPECT_FALSE(session_->TooltipPending());
}

TEST_F(TooltipTest, ItIsDrawnLastOverEverythingElse) {
  Frame(0.0, 50, 15);
  Frame(0.6, 50, 15);
  const auto& commands = renderer_->GetCommands();
  size_t tooltip_text = commands.size();
  size_t last_other_text = 0;
  for (size_t i = 0; i < commands.size(); i++) {
    if (commands[i].kind != Command::Kind::kText)
      continue;
    if (commands[i].text == "Save the file")
      tooltip_text = i;
    else
      last_other_text = i;
  }
  ASSERT_LT(tooltip_text, commands.size());
  EXPECT_GT(tooltip_text, last_other_text) << "after the text of the view";
  // its background comes after what the view drew, and before its text
  const auto box = TooltipBox();
  ASSERT_TRUE(box.found);
}

TEST_F(TooltipTest, ItIsBelowTheNodeWithThePointerInTheMiddle) {
  Frame(0.0, 80, 15);
  Frame(0.6, 80, 15);
  const auto box = TooltipBox();
  ASSERT_TRUE(box.found);
  EXPECT_GE(box.top, 30 + default_style_.gap - 0.5f) << "under the button, which ends at 30";
  EXPECT_NEAR((box.left + box.right) / 2, 80, 1.5) << "the pointer is in the middle";
  const auto text = Text("Save the file");
  EXPECT_GT(text->from.x, box.left);
  EXPECT_GT(text->from.y, box.top);
}

TEST_F(TooltipTest, ItGoesAboveTheNodeWhenThereIsNoRoomBelow) {
  Start(kWidth, 240);  // the last block ends at 230: no room for a tooltip under it
  Frame(0.0, 50, 215);
  Frame(0.6, 50, 215);
  const auto box = TooltipBox();
  ASSERT_TRUE(box.found);
  EXPECT_LE(box.bottom, 200 - default_style_.gap + 0.5f) << "above the block, which starts at 200";
  EXPECT_GE(box.top, default_style_.margin);
}

TEST_F(TooltipTest, ItStaysInsideTheViewport) {
  // the pointer is at the edge of the column, so that the middle of the tooltip would be outside the window
  Frame(0.0, 398, 20);
  Frame(0.6, 398, 20);
  EXPECT_TRUE(Shown("the column"));
  const auto box = TooltipBox();
  ASSERT_TRUE(box.found);
  EXPECT_LE(box.right, kWidth - default_style_.margin + 0.5f);
  EXPECT_GE(box.left, default_style_.margin);
}

TEST_F(TooltipTest, WhatHasNoTooltipHasTheOneOfWhatItIsIn) {
  Frame(0.0, 50, 55);  // the red block
  Frame(0.6, 50, 55);
  EXPECT_TRUE(Shown("the column"));
}

TEST_F(TooltipTest, WhatIsInsideHasItsOwn) {
  Frame(0.0, 50, 15);  // the button
  Frame(0.6, 50, 15);
  EXPECT_TRUE(Shown("Save the file"));
  EXPECT_FALSE(Shown("the column"));
}

TEST_F(TooltipTest, ATextCanHaveOne) {
  Frame(0.0, 20, 130);
  Frame(0.6, 20, 130);
  EXPECT_TRUE(Shown("about this"));
}

TEST_F(TooltipTest, ItGoesWhenThePointerLeaves) {
  Frame(0.0, 50, 15);
  Frame(0.6, 50, 15);
  ASSERT_TRUE(Shown("Save the file"));
  session_->PointerLeft();
  std::string error{};
  ASSERT_TRUE(session_->Render(&error)) << error;
  EXPECT_FALSE(Shown("Save the file"));
  EXPECT_FALSE(session_->TooltipPending());
}

TEST_F(TooltipTest, ItGoesWhenThePointerMovesToSomethingWithNoTooltipOfItsOwn) {
  Frame(0.0, 50, 15);
  Frame(0.6, 50, 15);
  ASSERT_TRUE(Shown("Save the file"));
  Frame(0.7, 450 - 100, 280);  // in the column's box? outside every node
  EXPECT_FALSE(Shown("Save the file"));
}

TEST_F(TooltipTest, TheDelayStartsOverWhenThePointerMovesToAnotherNode) {
  Frame(0.0, 50, 15);
  Frame(0.4, 50, 95);  // the block whose tooltip changes
  Frame(0.6, 50, 95);  // only 0.2 on it
  EXPECT_FALSE(Shown("state is 7"));
  EXPECT_TRUE(session_->TooltipPending());
  Frame(1.0, 50, 95);
  EXPECT_TRUE(Shown("state is 7"));
}

TEST_F(TooltipTest, MovingWithinANodeDoesntRestartTheDelay) {
  Frame(0.0, 20, 15);
  Frame(0.3, 60, 20);
  Frame(0.6, 80, 10);
  EXPECT_TRUE(Shown("Save the file"));
}

TEST_F(TooltipTest, AClickHidesItUntilThePointerIsOverSomethingElse) {
  Frame(0.0, 50, 15);
  Frame(0.6, 50, 15);
  ASSERT_TRUE(Shown("Save the file"));
  bool handled = false;
  std::string error{};
  ASSERT_TRUE(session_->Click(50, 15, &handled, &error)) << error;
  EXPECT_TRUE(handled);
  Frame(0.7, 50, 15);
  EXPECT_FALSE(Shown("Save the file"));
  Frame(5.0, 50, 15);
  EXPECT_FALSE(Shown("Save the file")) << "it stays hidden however long the pointer stays";
  EXPECT_FALSE(session_->TooltipPending());

  // over something else, and back again, it comes as it did
  Frame(5.1, 50, 55);
  Frame(5.2, 50, 15);
  Frame(5.8, 50, 15);
  EXPECT_TRUE(Shown("Save the file"));
}

TEST_F(TooltipTest, TheDelayCanBeChanged) {
  auto style = session_->GetTooltipStyle();
  style.delay = 0.1;
  session_->SetTooltipStyle(style);
  Frame(0.0, 50, 15);
  Frame(0.2, 50, 15);
  EXPECT_TRUE(Shown("Save the file"));
}

TEST_F(TooltipTest, ATooltipThatIsMadeWhenTheViewRunsStaysItself) {
  // its text is a new string every frame, but it is the same tooltip, so the delay isn't started again by each one
  Frame(0.0, 50, 95);
  Frame(0.2, 50, 95);
  Frame(0.4, 50, 95);
  Frame(0.6, 50, 95);
  EXPECT_TRUE(Shown("state is 7"));
}

TEST_F(TooltipTest, ALongTextIsWrappedToTheWidestATooltipIs) {
  Frame(0.0, 50, 215);
  Frame(0.6, 50, 215);
  const auto box = TooltipBox();
  ASSERT_TRUE(box.found);
  EXPECT_LE(box.width(), default_style_.max_width + 0.5f);
  int lines = 0;
  for (const auto& text : Texts()) {
    if (text.from.y > box.top && text.from.y < box.bottom && text.from.x >= box.left)
      lines++;
  }
  EXPECT_GT(lines, 1) << "it is broken into lines";
}

TEST_F(TooltipTest, WhatIsScrolledOutOfSightHasNoneWhereItIsNotSeen) {
  // the tall block is 100 high in a scroll box that is 40 high, at 150
  Frame(0.0, 50, 170);
  Frame(0.6, 50, 170);
  EXPECT_TRUE(Shown("tall"));

  Frame(1.0, 50, 150 + 70);  // inside the block, but under the scroll box
  Frame(1.6, 50, 150 + 70);
  EXPECT_FALSE(Shown("tall"));
}
}  // namespace
