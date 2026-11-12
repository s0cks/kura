// The built-in messages (Resized(Size), Focused(Bool)): the types every program has without declaring them, and the
// messages the host sends about the world around the UI.
#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "backend/program.h"
#include "backend/session.h"
#include "renderer/headless/renderer.h"

namespace {
using kura::Session;
using kura::renderer::HeadlessOptions;
using kura::renderer::HeadlessRenderer;
namespace rt = kura::rt;

constexpr uint32_t kWidth = 1200;
constexpr uint32_t kHeight = 800;

auto Programs(const std::string& name) -> std::string {
  return std::string(KURA_SESSION_PROGRAMS_DIR) + "/" + name;
}

auto Field(const rt::KuraValue& record, const std::string& name) -> rt::KuraValue {
  return kura_rt_record_get(reinterpret_cast<void*>(record.bits), rt::HashName(name));
}

auto FloatField(const rt::KuraValue& record, const std::string& name) -> double {
  const auto value = Field(record, name);
  double out = 0;
  std::memcpy(&out, &value.bits, sizeof(out));
  return out;
}

auto IntField(const rt::KuraValue& record, const std::string& name) -> int64_t {
  return static_cast<int64_t>(Field(record, name).bits);
}

struct Heard {
  std::string message;
  rt::KuraValue previous;
  rt::KuraValue state;
};

class BuiltinMessagesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    renderer_ = std::make_unique<HeadlessRenderer>(HeadlessOptions{.rasterize = false});
    abi_ = renderer_->Abi();
    ASSERT_EQ(kura_rt_set_renderer(&abi_), KURA_RENDERER_OK);
    kura_rt_set_viewport(kWidth, kHeight);
  }

  void TearDown() override {
    kura_rt_events_clear();
    kura_rt_set_renderer(nullptr);
  }

  void Start(const std::string& path) {
    std::string error{};
    program_ = kura::Program::Load(path, {}, &error);
    ASSERT_NE(program_, nullptr) << error;
    session_ = Session::Create(program_.get(), &error);
    ASSERT_NE(session_, nullptr) << error;
    session_->AddListener([this](const Session::Event& event) {
      heard_.push_back(Heard{rt::Format(event.message), event.previous, event.state});
    });
  }

  // The first point a click would send `name` from, scanning rows top to bottom.
  auto FindClick(const std::string& name, double* x, double* y) -> bool {
    for (uint32_t py = 0; py < kHeight; py++) {
      for (uint32_t px = 0; px < kWidth; px++) {
        rt::KuraValue message{};
        if (kura_rt_event_hit(rt::kEventClick, px + 0.5, py + 0.5, &message) != 0 && rt::Format(message) == name) {
          *x = px + 0.5;
          *y = py + 0.5;
          return true;
        }
      }
    }
    return false;
  }

  std::unique_ptr<HeadlessRenderer> renderer_;
  KuraRenderer abi_{};
  std::unique_ptr<kura::Program> program_;
  std::unique_ptr<Session> session_;
  std::vector<Heard> heard_{};
};

TEST(BuiltinMessages, TheHostBuildsThemAsTheValuesCompiledCodeDoes) {
  const auto resized = rt::MakeResized(640, 480);
  EXPECT_TRUE(rt::IsBuiltinMessage(resized));
  EXPECT_TRUE(rt::IsBuiltinMessage(rt::MakeFocused(true)));
  EXPECT_EQ(resized.tag, rt::VariantTag("Resized"));
  EXPECT_EQ(rt::Format(rt::MakeSize(3, 4)), "{ width: 3.0, height: 4.0 }");
  // a program's own messages aren't built in, whatever they're called
  EXPECT_FALSE(rt::IsBuiltinMessage(rt::MakeVariant("Increment")));
  EXPECT_FALSE(rt::IsBuiltinMessage(rt::MakeVariant("Tick")));
}

TEST_F(BuiltinMessagesTest, AProgramThatListsResizedGetsItThroughUpdate) {
  Start(std::string(KURA_EXAMPLES_DIR) + "/counter.kui");
  std::string error{};
  ASSERT_TRUE(session_->Resized(1000, 600, &error)) << error;
  const auto& state = session_->GetState();
  EXPECT_EQ(FloatField(state, "width"), 1000.0);
  EXPECT_EQ(FloatField(state, "height"), 600.0);
  EXPECT_EQ(IntField(state, "value"), 1);  // nothing else changed

  // and then the listener, which sees the state update made
  ASSERT_EQ(heard_.size(), 1u);
  EXPECT_NE(heard_[0].message.find("Resized"), std::string::npos);
  EXPECT_EQ(FloatField(heard_[0].previous, "width"), 0.0);
  EXPECT_EQ(FloatField(heard_[0].state, "width"), 1000.0);
}

TEST_F(BuiltinMessagesTest, TheViewTakesAdvantageOfTheSpaceItIsTold) {
  Start(std::string(KURA_EXAMPLES_DIR) + "/counter.kui");
  std::string error{};
  double up_x = 0, up_y = 0, down_x = 0, down_y = 0;

  // before it's told anything, the view has no width to use, and stacks its buttons
  ASSERT_TRUE(session_->Render(&error)) << error;
  ASSERT_TRUE(FindClick("Increment", &up_x, &up_y));
  ASSERT_TRUE(FindClick("Decrement", &down_x, &down_y));
  EXPECT_GT(down_y, up_y);

  // told the viewport is wide, it puts them side by side
  ASSERT_TRUE(session_->Resized(1000, 600, &error)) << error;
  ASSERT_TRUE(session_->Render(&error)) << error;
  ASSERT_TRUE(FindClick("Increment", &up_x, &up_y));
  ASSERT_TRUE(FindClick("Decrement", &down_x, &down_y));
  EXPECT_EQ(down_y, up_y);
  EXPECT_GT(down_x, up_x);

  // and narrow again
  ASSERT_TRUE(session_->Resized(400, 600, &error)) << error;
  ASSERT_TRUE(session_->Render(&error)) << error;
  ASSERT_TRUE(FindClick("Increment", &up_x, &up_y));
  ASSERT_TRUE(FindClick("Decrement", &down_x, &down_y));
  EXPECT_GT(down_y, up_y);
}

TEST_F(BuiltinMessagesTest, AProgramThatDoesNotListABuiltInMessageNeverSeesItButTheListenersDo) {
  Start(Programs("plain.kui"));
  std::string error{};
  ASSERT_TRUE(session_->Resized(800, 600, &error)) << error;
  ASSERT_TRUE(session_->Focused(true, &error)) << error;
  // not an error, and update never ran: the state is what it was
  EXPECT_EQ(IntField(session_->GetState(), "value"), 0);
  ASSERT_EQ(heard_.size(), 2u);
  EXPECT_NE(heard_[0].message.find("Resized"), std::string::npos);
  EXPECT_NE(heard_[1].message.find("Focused"), std::string::npos);
  EXPECT_EQ(heard_[0].previous.bits, heard_[0].state.bits);

  // and its own messages still work
  ASSERT_TRUE(session_->Send(rt::MakeVariant("Bump"), &error)) << error;
  EXPECT_EQ(IntField(session_->GetState(), "value"), 1);
}

TEST_F(BuiltinMessagesTest, SystemMessageAsksForAllOfThem) {
  Start(Programs("system.kui"));
  std::string error{};
  ASSERT_TRUE(session_->Resized(1280, 720, &error)) << error;
  ASSERT_TRUE(session_->Resized(1920, 1080, &error)) << error;
  ASSERT_TRUE(session_->Focused(true, &error)) << error;
  ASSERT_TRUE(session_->Send(rt::MakeVariant("Bump"), &error)) << error;

  const auto& state = session_->GetState();
  EXPECT_EQ(IntField(state, "resizes"), 2);
  EXPECT_EQ(FloatField(state, "width"), 1920.0);
  EXPECT_EQ(FloatField(state, "height"), 1080.0);
  EXPECT_EQ(Field(state, "focused").bits, 1u);
  EXPECT_EQ(IntField(state, "bumps"), 1);

  ASSERT_TRUE(session_->Focused(false, &error)) << error;
  EXPECT_EQ(Field(session_->GetState(), "focused").bits, 0u);
}

TEST_F(BuiltinMessagesTest, ListenersHearBuiltInMessagesInOrderWithTheRest) {
  Start(Programs("system.kui"));
  std::string error{};
  ASSERT_TRUE(session_->Send(rt::MakeVariant("Bump"), &error)) << error;
  ASSERT_TRUE(session_->Resized(10, 20, &error)) << error;
  ASSERT_TRUE(session_->Focused(true, &error)) << error;
  ASSERT_EQ(heard_.size(), 3u);
  EXPECT_EQ(heard_[0].message, "Bump");
  EXPECT_NE(heard_[1].message.find("Resized"), std::string::npos);
  EXPECT_NE(heard_[2].message.find("Focused"), std::string::npos);
}

TEST(BuiltinMessages, AProgramCantGiveABuiltInMessageAnotherPayload) {
  std::string error{};
  EXPECT_EQ(kura::Program::Load(Programs("bad-payload.kui"), {}, &error), nullptr);
  EXPECT_NE(error.find("`Resized` is a built-in message, which carries a Size"), std::string::npos) << error;
}

TEST(BuiltinMessages, AProgramCantDeclareABuiltInType) {
  std::string error{};
  EXPECT_EQ(kura::Program::Load(Programs("own-size.kui"), {}, &error), nullptr);
  EXPECT_NE(error.find("`Size` is a built-in type"), std::string::npos) << error;
}

TEST(BuiltinMessages, AProgramsOwnTickIsStillItsOwn) {
  std::string error{};
  const auto program = kura::Program::Load(Programs("own-tick.kui"), {}, &error);
  ASSERT_NE(program, nullptr) << error;
  rt::KuraValue result{};
  ASSERT_TRUE(program->Call("update", {rt::MakeVariant("Tick"), rt::MakeInt(4)}, &result, &error)) << error;
  EXPECT_EQ(result.bits, 5u);
}
}  // namespace
