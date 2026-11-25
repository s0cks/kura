// The view a program runs in, and what the runtime keeps in it.
#include <gtest/gtest.h>

#include "view.h"

namespace {
using kura::View;

struct Counter : View::ViewState {
  int n = 0;
};

struct Other : View::ViewState {
  int n = 100;
};

// the views a test made are gone when it is
class ViewTest : public ::testing::Test {
 protected:
  void TearDown() override {
    View::SetBase(nullptr);
    View::Unwind(0);
  }
};

TEST_F(ViewTest, StateBelongsToTheViewThatKeptIt) {
  const auto a = View::New();
  const auto b = View::New();
  a->GetState<Counter>().n = 3;
  EXPECT_EQ(a->GetState<Counter>().n, 3);
  EXPECT_EQ(b->GetState<Counter>().n, 0);  // made for b when it first asked, not shared with a
  View::Destroy(a);
  View::Destroy(b);
}

TEST_F(ViewTest, EachTypeOfStateIsItsOwn) {
  const auto view = View::New();
  view->GetState<Counter>().n = 1;
  EXPECT_EQ(view->GetState<Other>().n, 100);
  EXPECT_EQ(view->GetState<Counter>().n, 1);
  View::Destroy(view);
}

TEST_F(ViewTest, TheCurrentViewIsTheEnteredOneThenTheBase) {
  const auto base = View::New();
  const auto inner = View::New();
  View::SetBase(base);
  EXPECT_EQ(&View::Current(), base);

  View::Enter(inner);
  EXPECT_EQ(&View::Current(), inner);
  View::Leave();
  EXPECT_EQ(&View::Current(), base);

  View::Destroy(base);
  View::Destroy(inner);
}

TEST_F(ViewTest, EnteringNoViewStaysInTheOneItIsIn) {
  // `update` called by a test, which wasn't given a view, runs in the view of whoever called it
  const auto view = View::New();
  View::Enter(view);
  View::Enter(nullptr);
  EXPECT_EQ(&View::Current(), view);
  View::Leave();
  EXPECT_EQ(&View::Current(), view);
  View::Leave();
  View::Destroy(view);
}

TEST_F(ViewTest, ThereIsAlwaysAViewToBeIn) {
  // a program that isn't run by a host has the view of its thread, and the same one each time
  EXPECT_EQ(&View::Current(), &View::Current());
}

TEST_F(ViewTest, UnwindLeavesWhatAPanicJumpedOutOf) {
  const auto view = View::New();
  const auto depth = View::Depth();
  View::Enter(view);
  View::Enter(view);
  EXPECT_EQ(View::Depth(), depth + 2);
  View::Unwind(depth);
  EXPECT_EQ(View::Depth(), depth);
  View::Destroy(view);
}

TEST_F(ViewTest, ADestroyedViewIsNotFoundAndIsNoLongerTheBase) {
  const auto view = View::New();
  const auto id = view->GetViewId();
  EXPECT_EQ(View::Get(id), view);
  View::SetBase(view);
  View::Destroy(view);
  EXPECT_EQ(View::Get(id), nullptr);
  EXPECT_EQ(View::GetBase(), nullptr);
}
}  // namespace
