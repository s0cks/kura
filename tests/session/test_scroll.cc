// Scrolling: `#scroll` and `#list` keep how far they are scrolled between frames, move their content by it, clip
// what is in them, and say so with `on-scroll`. tests/session/programs/scroll.kui is a scroll container with six
// buttons in it, about two and a half of which fit.
#include <gtest/gtest.h>

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
namespace rt = kura::rt;

constexpr uint32_t kWidth = 200;
constexpr uint32_t kHeight = 300;

class ScrollTest : public ::testing::Test {
 protected:
  void SetUp() override {
    renderer_ = std::make_unique<HeadlessRenderer>(HeadlessOptions{.rasterize = false});
    abi_ = renderer_->Abi();
    ASSERT_EQ(kura_rt_set_renderer(&abi_), KURA_RENDERER_OK);
    kura_rt_set_viewport(kWidth, kHeight);
    std::string error{};
    program_ = kura::Program::Load(std::string(KURA_SESSION_PROGRAMS_DIR) + "/scroll.kui", {}, &error);
    ASSERT_NE(program_, nullptr) << error;
    session_ = Session::Create(program_.get(), &error);
    ASSERT_NE(session_, nullptr) << error;
    Render();
  }

  void TearDown() override {
    kura_rt_events_clear();
    kura_rt_set_renderer(nullptr);
  }

  void Render() {
    std::string error{};
    ASSERT_TRUE(session_->Render(&error)) << error;
  }

  // The wheel at (x, y); whether the container took it.
  auto Wheel(const double x, const double y, const double dx, const double dy) -> bool {
    bool handled = false;
    std::string error{};
    EXPECT_TRUE(session_->Scroll(x, y, dx, dy, &handled, &error)) << error;
    return handled;
  }

  // Where the text `label` was drawn last frame: its top, or -1000 if it wasn't.
  auto TextY(const std::string& label) const -> float {
    for (const auto& command : renderer_->GetCommands()) {
      if (command.kind == Command::Kind::kText && command.text == label)
        return command.from.y;
    }
    return -1000;
  }

  auto Count(const Command::Kind kind) const -> int {
    int count = 0;
    for (const auto& command : renderer_->GetCommands())
      count += command.kind == kind ? 1 : 0;
    return count;
  }

  auto Scrolls() const -> int64_t {
    return static_cast<int64_t>(session_->GetState().bits);
  }

  std::unique_ptr<HeadlessRenderer> renderer_;
  KuraRenderer abi_{};
  std::unique_ptr<kura::Program> program_;
  std::unique_ptr<Session> session_;
};

TEST_F(ScrollTest, ContentStartsAtTheTop) {
  EXPECT_FLOAT_EQ(TextY("a"), 2);
  EXPECT_GT(TextY("f"), 60) << "the rows that don't fit are laid out below the container's box";
}

TEST_F(ScrollTest, TheContentIsClippedToTheContainer) {
  EXPECT_EQ(Count(Command::Kind::kPushClip), 1);
  EXPECT_EQ(Count(Command::Kind::kPopClip), 1);
  for (const auto& command : renderer_->GetCommands()) {
    if (command.kind == Command::Kind::kPushClip) {
      EXPECT_FLOAT_EQ(command.rect.y, 0);
      EXPECT_FLOAT_EQ(command.rect.height, 60);
    }
  }
}

TEST_F(ScrollTest, TheWheelMovesTheContentOnTheNextFrame) {
  const auto before = TextY("b");
  EXPECT_TRUE(Wheel(10, 10, 0, 20));
  EXPECT_FLOAT_EQ(TextY("b"), before) << "nothing changes until the view runs again";
  Render();
  EXPECT_FLOAT_EQ(TextY("b"), before - 20);
}

TEST_F(ScrollTest, ScrollingStaysWhereItWasAcrossFrames) {
  EXPECT_TRUE(Wheel(10, 10, 0, 20));
  Render();
  Render();
  EXPECT_FLOAT_EQ(TextY("a"), 2 - 20);
}

TEST_F(ScrollTest, ItStopsAtTheEndOfTheContent) {
  EXPECT_TRUE(Wheel(10, 10, 0, 100000));
  Render();
  const auto last = TextY("f");
  EXPECT_LE(last, 60) << "the last row has come into view";
  EXPECT_FALSE(Wheel(10, 10, 0, 100)) << "there is nowhere further to go";
}

TEST_F(ScrollTest, ItStopsAtTheStart) {
  EXPECT_FALSE(Wheel(10, 10, 0, -50)) << "it is at the top";
  EXPECT_TRUE(Wheel(10, 10, 0, 30));
  EXPECT_TRUE(Wheel(10, 10, 0, -1000));
  Render();
  EXPECT_FLOAT_EQ(TextY("a"), 2);
}

TEST_F(ScrollTest, AWheelOutsideTheContainerIsTheHosts) {
  EXPECT_FALSE(Wheel(10, 250, 0, 20));
}

TEST_F(ScrollTest, OnScrollSendsItsMessageThroughUpdate) {
  EXPECT_EQ(Scrolls(), 0);
  EXPECT_TRUE(Wheel(10, 10, 0, 20));
  EXPECT_EQ(Scrolls(), 1);
  EXPECT_TRUE(Wheel(10, 10, 0, 20));
  EXPECT_EQ(Scrolls(), 2);
  EXPECT_FALSE(Wheel(10, 250, 0, 20));
  EXPECT_EQ(Scrolls(), 2) << "a wheel that scrolled nothing says nothing";
}

TEST_F(ScrollTest, AClickInTheContentIsAClickOnWhatIsThere) {
  rt::KuraValue message{};
  EXPECT_EQ(kura_rt_event_hit(rt::kEventClick, 5, 5, &message), 1);
  EXPECT_EQ(rt::Format(message), "Clicked");
}

TEST_F(ScrollTest, WhatIsScrolledOutOfSightCantBeClicked) {
  // the sixth row is far below the container before scrolling: its place is cut to the container's
  rt::KuraValue message{};
  const auto below = TextY("f");
  ASSERT_GT(below, 60);
  EXPECT_EQ(kura_rt_event_hit(rt::kEventClick, 5, below + 1, &message), 0);

  // after scrolling to the end, the first is above the container and can't be clicked where it was drawn
  EXPECT_TRUE(Wheel(10, 10, 0, 100000));
  Render();
  const auto above = TextY("a");
  ASSERT_LT(above, 0);
  EXPECT_EQ(kura_rt_event_hit(rt::kEventClick, 5, above + 1, &message), 0);
}
}  // namespace
