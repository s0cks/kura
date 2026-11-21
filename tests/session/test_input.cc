// Inputs: an `#input` takes the keyboard when it is clicked, and what is typed in it is the Message `on-input` names,
// with the new text in it. tests/session/programs/input.kui is a column of them.
#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "backend/program.h"
#include "backend/session.h"
#include "object.h"
#include "renderer/headless/renderer.h"

namespace {
using kura::Session;
using kura::renderer::Command;
using kura::renderer::HeadlessOptions;
using kura::renderer::HeadlessRenderer;
namespace rt = kura::rt;
using Key = rt::input::Key;

constexpr uint32_t kWidth = 400;
constexpr uint32_t kHeight = 300;

class InputTest : public ::testing::Test {
 protected:
  void SetUp() override {
    renderer_ = std::make_unique<HeadlessRenderer>(HeadlessOptions{.rasterize = false});
    abi_ = renderer_->Abi();
    ASSERT_EQ(kura_rt_set_renderer(&abi_), KURA_RENDERER_OK);
    kura_rt_set_viewport(kWidth, kHeight);
    std::string error{};
    program_ = kura::Program::Load(std::string(KURA_SESSION_PROGRAMS_DIR) + "/input.kui", {}, &error);
    ASSERT_NE(program_, nullptr) << error;
    session_ = Session::Create(program_.get(), &error);
    ASSERT_NE(session_, nullptr) << error;
    Render();
  }

  void TearDown() override {
    session_.reset();
    kura_rt_events_clear();
    kura_rt_set_renderer(nullptr);
  }

  void Render() {
    std::string error{};
    ASSERT_TRUE(session_->Render(&error)) << error;
  }

  // The inputs of the frame, top to bottom: where each is.
  auto InputAt(const int index) -> KuraRect {
    std::vector<KuraRect> found{};
    for (float y = 0; y < kHeight; y += 1) {
      KuraRect box{};
      rt::KuraValue message{};
      if (kura_rt_event_topmost(rt::kEventInput, 5, y + 0.5, &box, &message) == 0)
        continue;
      if (found.empty() || found.back().y != box.y)
        found.push_back(box);
    }
    EXPECT_GT(found.size(), static_cast<size_t>(index));
    return found.size() > static_cast<size_t>(index) ? found[index] : KuraRect{};
  }

  void ClickInput(const int index, const float dx = 5) {
    const auto box = InputAt(index);
    bool handled = false;
    std::string error{};
    ASSERT_TRUE(session_->Click(box.x + dx, box.y + box.height / 2, &handled, &error)) << error;
    EXPECT_TRUE(handled);
  }

  // false when the keyboard wasn't an input's
  auto Type(const std::string& text) -> bool {
    bool handled = false;
    std::string error{};
    EXPECT_TRUE(session_->TextInput(text, &handled, &error)) << error;
    return handled;
  }

  auto Press(const Key key, const int mods = 0) -> bool {
    bool handled = false;
    std::string error{};
    EXPECT_TRUE(session_->KeyDown(key, mods, &handled, &error)) << error;
    return handled;
  }

  auto Field(const char* name) const -> std::string {
    const auto value = kura_rt_record_get(reinterpret_cast<void*>(session_->GetState().bits), rt::HashName(name));
    return std::string(rt::StringView(value.bits));
  }

  // What was drawn as text in the last frame.
  auto Texts() const -> std::vector<std::string> {
    std::vector<std::string> texts{};
    for (const auto& command : renderer_->GetCommands()) {
      if (command.kind == Command::Kind::kText)
        texts.push_back(command.text);
    }
    return texts;
  }

  std::unique_ptr<HeadlessRenderer> renderer_;
  KuraRenderer abi_{};
  std::unique_ptr<kura::Program> program_;
  std::unique_ptr<Session> session_;
};

TEST_F(InputTest, InputsAreDrawnWithTheirText) {
  const auto texts = Texts();
  EXPECT_NE(std::find(texts.begin(), texts.end(), "hi"), texts.end());
  EXPECT_NE(std::find(texts.begin(), texts.end(), "fixed"), texts.end());
  EXPECT_NE(std::find(texts.begin(), texts.end(), "plain"), texts.end());
  // an input that is empty shows what it says is to be put in it
  EXPECT_NE(std::find(texts.begin(), texts.end(), "Pick"), texts.end());
  EXPECT_EQ(std::find(texts.begin(), texts.end(), "Name"), texts.end());
}

TEST_F(InputTest, AnInputIsWhatItIsToldAndNotWhatItsTextIs) {
  const auto first = InputAt(0);
  EXPECT_FLOAT_EQ(first.width, 200.0f);
  EXPECT_GT(first.height, 8.0f);
  // all the same size, and apart by the gap
  EXPECT_FLOAT_EQ(InputAt(1).width, 200.0f);
  EXPECT_FLOAT_EQ(InputAt(1).y, first.y + first.height + 10.0f);
}

TEST_F(InputTest, NothingHasTheKeyboardUntilAnInputIsClicked) {
  EXPECT_FALSE(session_->HasTextFocus());
  EXPECT_FALSE(Type("a"));
  EXPECT_FALSE(Press(Key::kBackspace));
  EXPECT_EQ(Field("text"), "hi");
  ClickInput(0);
  EXPECT_TRUE(session_->HasTextFocus());
}

TEST_F(InputTest, TypedTextIsAMessageWithTheNewText) {
  ClickInput(0, 150);  // past the end of the text
  EXPECT_TRUE(Type("!"));
  EXPECT_EQ(Field("text"), "hi!");
  Render();
  const auto texts = Texts();
  EXPECT_NE(std::find(texts.begin(), texts.end(), "hi!"), texts.end());
}

TEST_F(InputTest, TextTypedBeforeTheNextFrameIsNotLost) {
  ClickInput(0, 150);
  EXPECT_TRUE(Type("a"));
  EXPECT_TRUE(Type("b"));
  EXPECT_TRUE(Press(Key::kBackspace));
  EXPECT_TRUE(Type("c"));
  EXPECT_EQ(Field("text"), "hiac");
}

TEST_F(InputTest, TheCaretGoesWhereTheClickWas) {
  ClickInput(0, 1);  // before the first letter
  EXPECT_TRUE(Type(">"));
  EXPECT_EQ(Field("text"), ">hi");
  Render();
  EXPECT_TRUE(Press(Key::kEnd));
  EXPECT_TRUE(Type("<"));
  EXPECT_EQ(Field("text"), ">hi<");
  EXPECT_TRUE(Press(Key::kLeft));
  EXPECT_TRUE(Press(Key::kLeft));
  EXPECT_TRUE(Type("-"));
  EXPECT_EQ(Field("text"), ">h-i<");
  EXPECT_TRUE(Press(Key::kHome));
  EXPECT_TRUE(Press(Key::kDelete));
  EXPECT_EQ(Field("text"), "h-i<");
}

TEST_F(InputTest, ASelectionIsReplacedByWhatIsTyped) {
  ClickInput(0, 150);
  EXPECT_TRUE(Press(Key::kSelectAll));
  EXPECT_EQ(session_->SelectedText(), "hi");
  EXPECT_TRUE(Type("yo"));
  EXPECT_EQ(Field("text"), "yo");

  EXPECT_TRUE(Press(Key::kHome));
  EXPECT_TRUE(Press(Key::kRight, rt::input::kShift));
  EXPECT_EQ(session_->SelectedText(), "y");
  EXPECT_TRUE(Press(Key::kBackspace));
  EXPECT_EQ(Field("text"), "o");
}

TEST_F(InputTest, ControlMovesAndDeletesByWords) {
  ClickInput(0, 150);
  EXPECT_TRUE(Type(" there you"));
  EXPECT_EQ(Field("text"), "hi there you");
  EXPECT_TRUE(Press(Key::kBackspace, rt::input::kControl));
  EXPECT_EQ(Field("text"), "hi there ");
  EXPECT_TRUE(Press(Key::kLeft, rt::input::kControl));
  EXPECT_TRUE(Press(Key::kDelete, rt::input::kControl));
  EXPECT_EQ(Field("text"), "hi  ");
}

TEST_F(InputTest, PastedLinesAreOneLine) {
  ClickInput(0, 150);
  EXPECT_TRUE(Type("a\r\nb\tc"));
  EXPECT_EQ(Field("text"), "hia b c");
}

TEST_F(InputTest, LettersAreNotCutInHalf) {
  ClickInput(0, 150);
  EXPECT_TRUE(Type("é日"));
  EXPECT_TRUE(Press(Key::kBackspace));
  EXPECT_EQ(Field("text"), "hié");
  EXPECT_TRUE(Press(Key::kLeft));
  EXPECT_TRUE(Type("x"));
  EXPECT_EQ(Field("text"), "hixé");
}

TEST_F(InputTest, AReadonlyInputCanBeSelectedButNotChanged) {
  ClickInput(1, 150);
  EXPECT_TRUE(session_->HasTextFocus());
  EXPECT_FALSE(Type("a"));
  EXPECT_FALSE(Press(Key::kBackspace));
  EXPECT_TRUE(Press(Key::kSelectAll));
  EXPECT_EQ(session_->SelectedText(), "fixed");
  EXPECT_FALSE(session_->InputBlinking());
}

TEST_F(InputTest, AnInputWithNoOnInputOnlyShows) {
  ClickInput(2, 150);
  EXPECT_FALSE(Type("a"));
  EXPECT_FALSE(Press(Key::kDelete));
}

TEST_F(InputTest, WhatTheProgramDoesntTakeIsNotKept) {
  ClickInput(3);
  EXPECT_TRUE(Type("nope"));
  EXPECT_EQ(Field("asked"), "");
  Render();
  const auto texts = Texts();
  EXPECT_EQ(std::find(texts.begin(), texts.end(), "nope"), texts.end());
  EXPECT_NE(std::find(texts.begin(), texts.end(), "Pick"), texts.end());
}

TEST_F(InputTest, EscapeAndClicksElsewhereGiveTheKeyboardUp) {
  ClickInput(0, 150);
  EXPECT_TRUE(Press(Key::kEscape));
  EXPECT_FALSE(session_->HasTextFocus());
  EXPECT_FALSE(Type("a"));

  ClickInput(0, 150);
  bool handled = true;
  std::string error{};
  ASSERT_TRUE(session_->Click(390, 290, &handled, &error)) << error;
  EXPECT_FALSE(handled);
  EXPECT_FALSE(session_->HasTextFocus());
}

TEST_F(InputTest, ClickingAnotherInputMovesTheKeyboard) {
  ClickInput(0, 150);
  ClickInput(1, 150);
  EXPECT_FALSE(Type("a"));  // the readonly one
  ClickInput(0, 150);
  EXPECT_TRUE(Type("a"));
  EXPECT_EQ(Field("text"), "hia");
}

TEST_F(InputTest, TheCaretBlinksOnlyInAnInputThatCanBeChanged) {
  EXPECT_FALSE(session_->InputBlinking());
  ClickInput(0, 150);
  EXPECT_TRUE(session_->InputBlinking());
}


// What on-input names has to be able to carry the text.
TEST(InputProgramTest, OnInputNeedsAVariantThatCarriesAString) {
  auto renderer = std::make_unique<HeadlessRenderer>(HeadlessOptions{.rasterize = false});
  auto abi = renderer->Abi();
  ASSERT_EQ(kura_rt_set_renderer(&abi), KURA_RENDERER_OK);
  std::string error{};
  const auto program = kura::Program::Load(std::string(KURA_SESSION_PROGRAMS_DIR) + "/bad-input.kui", {}, &error);
  EXPECT_EQ(program, nullptr);
  EXPECT_NE(error.find("has to carry a String"), std::string::npos) << error;
  kura_rt_set_renderer(nullptr);
}

TEST(InputProgramTest, TheExampleCompilesAndRenders) {
  auto renderer = std::make_unique<HeadlessRenderer>(HeadlessOptions{.rasterize = false});
  auto abi = renderer->Abi();
  ASSERT_EQ(kura_rt_set_renderer(&abi), KURA_RENDERER_OK);
  kura_rt_set_viewport(kWidth, kHeight);
  std::string error{};
  const auto program = kura::Program::Load(std::string(KURA_EXAMPLES_DIR) + "/test-input.kui", {}, &error);
  ASSERT_NE(program, nullptr) << error;
  auto session = Session::Create(program.get(), &error);
  ASSERT_NE(session, nullptr) << error;
  ASSERT_TRUE(session->Render(&error)) << error;
  kura_rt_events_clear();
  kura_rt_set_renderer(nullptr);
}
}  // namespace
