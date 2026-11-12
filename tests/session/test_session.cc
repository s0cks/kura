// Messages between the UI and its host, through the Session: examples/counter.kui, whose `update` handles
// Increment, Decrement, Reset and Set(Int), and whose view has an Increment button and a Decrement button.
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

constexpr uint32_t kWidth = 400;
constexpr uint32_t kHeight = 300;

auto Message(const std::string& name) -> rt::KuraValue {
  return rt::MakeVariant(name);
}

auto SetMessage(const int64_t value) -> rt::KuraValue {
  const rt::KuraValue payload{rt::kTagInt, static_cast<uint64_t>(value)};
  return rt::MakeVariant("Set", &payload);
}

// The counter's `value`, which is what these tests are about: its state also keeps the size of the viewport.
auto ValueOf(const rt::KuraValue& state) -> int64_t {
  const auto field = kura_rt_record_get(reinterpret_cast<void*>(state.bits), rt::HashName("value"));
  return static_cast<int64_t>(field.bits);
}

// `{ value: 2 }`
auto Describe(const rt::KuraValue& state) -> std::string {
  return "{ value: " + std::to_string(ValueOf(state)) + " }";
}

// What a listener saw of one Message.
struct Heard {
  std::string message;
  std::string previous;
  std::string state;
  int listener = 0;
};

class SessionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    renderer_ = std::make_unique<HeadlessRenderer>(HeadlessOptions{.rasterize = false});
    abi_ = renderer_->Abi();
    ASSERT_EQ(kura_rt_set_renderer(&abi_), KURA_RENDERER_OK);
    kura_rt_set_viewport(kWidth, kHeight);
    std::string error{};
    program_ = kura::Program::Load(std::string(KURA_EXAMPLES_DIR) + "/counter.kui", {}, &error);
    ASSERT_NE(program_, nullptr) << error;
    session_ = Session::Create(program_.get(), &error);
    ASSERT_NE(session_, nullptr) << error;
  }

  void TearDown() override {
    kura_rt_events_clear();
    kura_rt_set_renderer(nullptr);
  }

  auto State() const -> std::string {
    return Describe(session_->GetState());
  }

  // A listener that writes what it heard into `heard_`.
  auto Listen(const int id) -> Session::ListenerId {
    return session_->AddListener([this, id](const Session::Event& event) {
      heard_.push_back(Heard{rt::Format(event.message), Describe(event.previous), Describe(event.state), id});
    });
  }

  // The view runs its own frame, at the size of the viewport the host set.
  void Render() {
    std::string error{};
    ASSERT_TRUE(session_->Render(&error)) << error;
  }

  // A point that a click would send `name` from, found by asking where the view said it means that.
  auto FindClick(const std::string& name, double* x, double* y) -> bool {
    for (uint32_t py = 0; py < kHeight; py++) {
      for (uint32_t px = 0; px < kWidth; px++) {
        rt::KuraValue message{};
        if (kura_rt_event_hit(rt::kEventClick, px + 0.5, py + 0.5, &message) != 0 &&
            rt::Format(message) == name) {
          *x = px + 0.5;
          *y = py + 0.5;
          return true;
        }
      }
    }
    return false;
  }

  // What the view's background was filled with, after the last frame: white while the value is positive.
  auto Background() const -> KuraColor {
    for (const auto& command : renderer_->GetCommands()) {
      if (command.kind == Command::Kind::kFillRect)
        return command.color;
    }
    ADD_FAILURE() << "the view filled nothing";
    return {};
  }

  std::unique_ptr<HeadlessRenderer> renderer_;
  KuraRenderer abi_{};
  std::unique_ptr<kura::Program> program_;
  std::unique_ptr<Session> session_;
  std::vector<Heard> heard_{};
};

TEST_F(SessionTest, StartsFromInit) {
  EXPECT_EQ(State(), "{ value: 1 }");
}

// host => UI
TEST_F(SessionTest, AMessageFromTheHostGoesThroughUpdate) {
  std::string error{};
  ASSERT_TRUE(session_->Send(Message("Increment"), &error)) << error;
  EXPECT_EQ(State(), "{ value: 2 }");
  ASSERT_TRUE(session_->Send(Message("Increment"), &error)) << error;
  ASSERT_TRUE(session_->Send(Message("Decrement"), &error)) << error;
  EXPECT_EQ(State(), "{ value: 2 }");
  ASSERT_TRUE(session_->Send(Message("Reset"), &error)) << error;
  EXPECT_EQ(State(), "{ value: 0 }");
}

TEST_F(SessionTest, AMessageCanCarryAPayload) {
  std::string error{};
  ASSERT_TRUE(session_->Send(SetMessage(42), &error)) << error;
  EXPECT_EQ(State(), "{ value: 42 }");
  ASSERT_TRUE(session_->Send(SetMessage(-7), &error)) << error;
  EXPECT_EQ(State(), "{ value: -7 }");
}

TEST_F(SessionTest, AMessageThatIsNotOneOfTheProgramsIsRefused) {
  Listen(1);
  std::string error{};
  EXPECT_FALSE(session_->Send(Message("NotAMessage"), &error));
  EXPECT_FALSE(error.empty());
  // nothing was delivered: not to update, and not to the listeners
  EXPECT_EQ(State(), "{ value: 1 }");
  EXPECT_TRUE(heard_.empty());
  // and the session carries on
  error.clear();
  EXPECT_TRUE(session_->Send(Message("Increment"), &error)) << error;
  EXPECT_EQ(State(), "{ value: 2 }");
}

// Listeners come after update
TEST_F(SessionTest, ListenersHearAMessageAfterUpdateHasMadeTheNewState) {
  Listen(1);
  std::string error{};
  ASSERT_TRUE(session_->Send(Message("Increment"), &error)) << error;
  ASSERT_EQ(heard_.size(), 1u);
  EXPECT_EQ(heard_[0].message, "Increment");
  EXPECT_EQ(heard_[0].previous, "{ value: 1 }");
  EXPECT_EQ(heard_[0].state, "{ value: 2 }");
}

TEST_F(SessionTest, ListenersAreCalledInTheOrderTheyWereAdded) {
  Listen(1);
  Listen(2);
  Listen(3);
  std::string error{};
  ASSERT_TRUE(session_->Send(Message("Decrement"), &error)) << error;
  ASSERT_EQ(heard_.size(), 3u);
  EXPECT_EQ(heard_[0].listener, 1);
  EXPECT_EQ(heard_[1].listener, 2);
  EXPECT_EQ(heard_[2].listener, 3);
  for (const auto& heard : heard_)
    EXPECT_EQ(heard.state, "{ value: 0 }");
}

TEST_F(SessionTest, ARemovedListenerHearsNothing) {
  const auto first = Listen(1);
  Listen(2);
  EXPECT_TRUE(session_->RemoveListener(first));
  EXPECT_FALSE(session_->RemoveListener(first));
  std::string error{};
  ASSERT_TRUE(session_->Send(Message("Increment"), &error)) << error;
  ASSERT_EQ(heard_.size(), 1u);
  EXPECT_EQ(heard_[0].listener, 2);
}

TEST_F(SessionTest, AListenerThatRemovesALaterOneStopsItBeingCalled) {
  Session::ListenerId second = 0;
  session_->AddListener([&](const Session::Event&) { session_->RemoveListener(second); });
  second = Listen(2);
  std::string error{};
  ASSERT_TRUE(session_->Send(Message("Increment"), &error)) << error;
  EXPECT_TRUE(heard_.empty());
}

TEST_F(SessionTest, AMessageUpdateDoesNotChangeStillReachesTheListeners) {
  Listen(1);
  std::string error{};
  ASSERT_TRUE(session_->Send(SetMessage(1), &error)) << error;  // the state already is 1
  ASSERT_EQ(heard_.size(), 1u);
  EXPECT_EQ(heard_[0].previous, heard_[0].state);
}

TEST_F(SessionTest, AMessageSentFromAListenerWaitsItsTurn) {
  // On the first Increment, the host answers with a Reset. The Reset isn't delivered inside the Increment's
  // delivery: every listener hears the Increment first, and the Messages are heard in the order they were sent.
  auto answered = false;
  session_->AddListener([&](const Session::Event& event) {
    if (rt::Format(event.message) == "Increment" && !answered) {
      answered = true;
      std::string error{};
      EXPECT_TRUE(session_->Send(Message("Reset"), &error)) << error;
      // still inside the Increment: nothing has changed under this listener
      EXPECT_EQ(Describe(session_->GetState()), "{ value: 2 }");
    }
  });
  Listen(2);
  std::string error{};
  ASSERT_TRUE(session_->Send(Message("Increment"), &error)) << error;

  ASSERT_EQ(heard_.size(), 2u);
  EXPECT_EQ(heard_[0].message, "Increment");
  EXPECT_EQ(heard_[0].state, "{ value: 2 }");
  EXPECT_EQ(heard_[1].message, "Reset");
  EXPECT_EQ(heard_[1].previous, "{ value: 2 }");
  EXPECT_EQ(heard_[1].state, "{ value: 0 }");
  EXPECT_EQ(State(), "{ value: 0 }");
}

// UI => host
TEST_F(SessionTest, TheViewSaysWhereAClickMeansAMessage) {
  Render();
  EXPECT_EQ(kura_rt_event_count(), 2u);
  double x = 0;
  double y = 0;
  EXPECT_TRUE(FindClick("Increment", &x, &y));
  EXPECT_TRUE(FindClick("Decrement", &x, &y));
}

TEST_F(SessionTest, AClickOnAButtonSendsItsMessageThroughUpdateAndThenToTheListeners) {
  Listen(1);
  Render();
  double x = 0;
  double y = 0;
  ASSERT_TRUE(FindClick("Increment", &x, &y));

  std::string error{};
  bool handled = false;
  ASSERT_TRUE(session_->Click(x, y, &handled, &error)) << error;
  EXPECT_TRUE(handled);
  EXPECT_EQ(State(), "{ value: 2 }");
  ASSERT_EQ(heard_.size(), 1u);
  EXPECT_EQ(heard_[0].message, "Increment");
  EXPECT_EQ(heard_[0].previous, "{ value: 1 }");
  EXPECT_EQ(heard_[0].state, "{ value: 2 }");

  ASSERT_TRUE(FindClick("Decrement", &x, &y));
  ASSERT_TRUE(session_->Click(x, y, &handled, &error)) << error;
  ASSERT_TRUE(session_->Click(x, y, &handled, &error)) << error;
  EXPECT_EQ(State(), "{ value: 0 }");
  EXPECT_EQ(heard_.size(), 3u);
}

TEST_F(SessionTest, AClickOnNothingIsNotAnError) {
  Listen(1);
  Render();
  std::string error{};
  bool handled = true;
  // the far corner is outside both buttons
  ASSERT_TRUE(session_->Click(kWidth - 0.5, kHeight - 0.5, &handled, &error)) << error;
  EXPECT_FALSE(handled);
  EXPECT_EQ(State(), "{ value: 1 }");
  EXPECT_TRUE(heard_.empty());
}

TEST_F(SessionTest, MessagesFromBothSidesShareOneStateAndOneListenerOrder) {
  Listen(1);
  Render();
  double x = 0;
  double y = 0;
  ASSERT_TRUE(FindClick("Increment", &x, &y));

  std::string error{};
  bool handled = false;
  ASSERT_TRUE(session_->Click(x, y, &handled, &error)) << error;  // UI => host: 2
  ASSERT_TRUE(session_->Send(SetMessage(10), &error)) << error;   // host => UI: 10
  ASSERT_TRUE(session_->Click(x, y, &handled, &error)) << error;  // UI => host: 11
  EXPECT_EQ(State(), "{ value: 11 }");
  ASSERT_EQ(heard_.size(), 3u);
  EXPECT_EQ(heard_[0].message, "Increment");
  EXPECT_EQ(heard_[1].message, "Set(10)");
  EXPECT_EQ(heard_[2].message, "Increment");
  EXPECT_EQ(heard_[2].previous, "{ value: 10 }");
}

TEST_F(SessionTest, TheViewFollowsTheState) {
  Render();
  EXPECT_EQ(Background().g, 1.0f);  // white
  std::string error{};
  ASSERT_TRUE(session_->Send(Message("Reset"), &error)) << error;
  Render();
  EXPECT_EQ(Background().r, 1.0f);  // red: the value is 0
  EXPECT_EQ(Background().g, 0.0f);
}

TEST_F(SessionTest, AFrameForgetsWhereTheLastOnesClicksWere) {
  Render();
  EXPECT_EQ(kura_rt_event_count(), 2u);
  Render();
  EXPECT_EQ(kura_rt_event_count(), 2u);
  kura_rt_begin_frame(kWidth, kHeight);
  EXPECT_EQ(kura_rt_event_count(), 0u);
  kura_rt_end_frame();
}
}  // namespace
