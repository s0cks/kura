// The heap and the escape analysis, as a program uses them: a view allocates in the frame's arena, which is reset
// for the next frame; a Message from the view is moved out of the frame before `update` can keep it; what stays in a
// function goes on the stack.
#include <gtest/gtest.h>

#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "backend/escape_analysis.h"
#include "backend/flow_graph.h"
#include "backend/ir.h"
#include "backend/program.h"
#include "backend/session.h"
#include "heap/heap.h"
#include "renderer/headless/renderer.h"

namespace {
using kura::Instruction;
using kura::Program;
using kura::Session;
using kura::renderer::HeadlessOptions;
using kura::renderer::HeadlessRenderer;
namespace rt = kura::rt;

auto Load(const std::string& name, std::string* error) -> std::unique_ptr<Program> {
  return Program::Load(std::string(KURA_SESSION_PROGRAMS_DIR) + "/" + name, {}, error);
}

// The instructions of the function called `name`, that `pick` says are the ones.
auto Find(Program* program, const std::string& name, const std::function<bool(Instruction*)>& pick)
    -> std::vector<Instruction*> {
  std::vector<Instruction*> found{};
  for (const auto graph : program->GetModuleGraph()->GetGraphs()) {
    if (graph->GetName() != name)
      continue;
    for (const auto block : graph->GetReversePostorder()) {
      for (Instruction* instr = block; instr != nullptr; instr = instr->GetNext()) {
        if (pick(instr))
          found.push_back(instr);
      }
    }
  }
  return found;
}

auto Stacked(Instruction* instr) -> bool {
  return instr->IsDefinition() && static_cast<kura::Definition*>(instr)->IsStackAllocated();
}

class EscapeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::string error{};
    program_ = Load("escape.kui", &error);
    ASSERT_NE(program_, nullptr) << error;
  }

  auto Lists(const std::string& function) -> std::vector<Instruction*> {
    return Find(program_.get(), function, [](Instruction* i) { return i->AsNewList() != nullptr; });
  }

  auto Records(const std::string& function) -> std::vector<Instruction*> {
    return Find(program_.get(), function, [](Instruction* i) { return i->AsNewRecord() != nullptr; });
  }

  std::unique_ptr<Program> program_;
};

TEST_F(EscapeTest, WhatIsOnlyReadStaysOnTheStack) {
  const auto lists = Lists("reads-a-list");
  ASSERT_EQ(lists.size(), 2U);
  for (const auto list : lists)
    EXPECT_TRUE(Stacked(list)) << list->ToString();

  const auto records = Records("reads-a-record");
  ASSERT_EQ(records.size(), 1U);
  EXPECT_TRUE(Stacked(records[0])) << records[0]->ToString();
}

TEST_F(EscapeTest, WhatIsReturnedLeaves) {
  ASSERT_EQ(Lists("returns-a-list").size(), 1U);
  EXPECT_FALSE(Stacked(Lists("returns-a-list")[0]));
  ASSERT_EQ(Records("returns-a-record").size(), 1U);
  EXPECT_FALSE(Stacked(Records("returns-a-record")[0]));
}

TEST_F(EscapeTest, WhatIsInSomethingThatLeavesLeavesToo) {
  const auto lists = Lists("nested-in-a-record");
  ASSERT_EQ(lists.size(), 1U);
  EXPECT_FALSE(Stacked(lists[0])) << lists[0]->ToString();
  ASSERT_EQ(Records("nested-in-a-record").size(), 1U);
  EXPECT_FALSE(Stacked(Records("nested-in-a-record")[0]));

  const auto outer = Lists("nested-in-a-list");
  ASSERT_EQ(outer.size(), 2U);
  for (const auto list : outer)
    EXPECT_FALSE(Stacked(list)) << list->ToString();
}

TEST_F(EscapeTest, WhatIsGivenToACallLeaves) {
  const auto lists = Lists("passes-to-a-call");
  ASSERT_EQ(lists.size(), 1U);
  EXPECT_FALSE(Stacked(lists[0]));
}

TEST_F(EscapeTest, AVariantTakenApartHereAndAClosureOnlyCalledHereStay) {
  const auto variants = Find(program_.get(), "unwraps", [](Instruction* i) {
    return i->AsNewVariant() != nullptr && i->AsNewVariant()->HasPayload();
  });
  ASSERT_EQ(variants.size(), 1U);
  EXPECT_TRUE(Stacked(variants[0])) << variants[0]->ToString();

  const auto closures =
      Find(program_.get(), "calls-a-closure", [](Instruction* i) { return i->AsNewClosure() != nullptr; });
  ASSERT_EQ(closures.size(), 1U);
  EXPECT_TRUE(Stacked(closures[0])) << closures[0]->ToString();
}

TEST_F(EscapeTest, WhatIsMadeInALoopIsNotOnTheStack) {
  // the `[n, n]` of the body, and the list the loop collects into (which is returned, too)
  const auto lists = Lists("loops");
  ASSERT_GE(lists.size(), 1U);
  for (const auto list : lists)
    EXPECT_FALSE(Stacked(list)) << list->ToString();
}

// What is on the stack works: the functions are run, and give what they should.
TEST_F(EscapeTest, StackObjectsWork) {
  const auto call = [&](const std::string& function, const std::vector<rt::KuraValue>& args) {
    rt::KuraValue result{};
    std::string error{};
    EXPECT_TRUE(program_->Call(function, args, &result, &error)) << error;
    return result;
  };
  EXPECT_EQ(static_cast<int64_t>(call("reads-a-list", {rt::MakeInt(3), rt::MakeInt(4)}).bits), 7);
  EXPECT_EQ(static_cast<int64_t>(call("reads-a-record", {rt::MakeInt(5)}).bits), 5);
  EXPECT_EQ(static_cast<int64_t>(call("unwraps", {rt::MakeInt(41)}).bits), 42);
  EXPECT_EQ(static_cast<int64_t>(call("calls-a-closure", {rt::MakeInt(9)}).bits), 10);
  EXPECT_EQ(rt::Format(call("returns-a-list", {rt::MakeInt(2)})), "[2, 2]");
  EXPECT_EQ(rt::Format(call("nested-in-a-record", {rt::MakeInt(6)})), "{ items: [6] }");
  EXPECT_EQ(rt::Format(call("nested-in-a-list", {rt::MakeInt(7)})), "[[7]]");
}

TEST_F(EscapeTest, TheStackMarkIsInTheInstructionsText) {
  const auto lists = Lists("reads-a-list");
  ASSERT_FALSE(lists.empty());
  EXPECT_NE(lists[0]->ToString().find("stack"), std::string::npos) << lists[0]->ToString();
}

TEST_F(EscapeTest, AnalyzingAgainChangesNothing) {
  uint64_t before = 0;
  for (const auto graph : program_->GetModuleGraph()->GetGraphs()) {
    if (graph->GetName() == "reads-a-list")
      before = kura::EscapeAnalysis::RunOn(graph);
  }
  EXPECT_EQ(before, 2U);
}

class HeapWiringTest : public ::testing::Test {
 protected:
  void SetUp() override {
    renderer_ = std::make_unique<HeadlessRenderer>(HeadlessOptions{.rasterize = false});
    abi_ = renderer_->Abi();
    ASSERT_EQ(kura_rt_set_renderer(&abi_), KURA_RENDERER_OK);
    kura_rt_set_viewport(200, 100);
    std::string error{};
    program_ = Load("heap.kui", &error);
    ASSERT_NE(program_, nullptr) << error;
    session_ = Session::Create(program_.get(), &error);
    ASSERT_NE(session_, nullptr) << error;
    heap_ = kura::heap::Heap::Current();
    ASSERT_NE(heap_, nullptr);
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

  void Click() {
    bool handled = false;
    std::string error{};
    ASSERT_TRUE(session_->Click(50, 20, &handled, &error)) << error;
    ASSERT_TRUE(handled);
  }

  auto Field(const char* name) const -> rt::KuraValue {
    return kura_rt_record_get(reinterpret_cast<void*>(session_->GetState().bits), rt::HashName(name));
  }

  std::unique_ptr<HeadlessRenderer> renderer_;
  KuraRenderer abi_{};
  std::unique_ptr<Program> program_;
  std::unique_ptr<Session> session_;
  kura::heap::Heap* heap_ = nullptr;
};

TEST_F(HeapWiringTest, AFrameIsAllocatedInTheFrameArenaAndResetForTheNext) {
  Render();
  const auto first = heap_->Stats().frame.used;
  EXPECT_GT(first, 0U);
  for (int i = 0; i < 200; i++)
    Render();
  // the arena is reset every frame, so it doesn't grow with the number of frames
  EXPECT_EQ(heap_->Stats().frame.used, first);
}

TEST_F(HeapWiringTest, TheViewDoesntAllocateInTheModel) {
  Render();
  const auto before = heap_->Stats().model.used;
  for (int i = 0; i < 50; i++)
    Render();
  EXPECT_EQ(heap_->Stats().model.used, before);
}

TEST_F(HeapWiringTest, AMessageFromTheViewIsMovedOutOfTheFrameBeforeUpdateKeepsIt) {
  Render();
  Click();
  EXPECT_EQ(rt::StringView(Field("said").bits), "count 0, said ");
  EXPECT_EQ(static_cast<int64_t>(Field("count").bits), 1);
  // nothing in the model points into the frame
  EXPECT_TRUE(heap_->Verify(&std::cerr));
  // and it is still there when the frame it was made in is gone
  Render();
  Render();
  EXPECT_EQ(rt::StringView(Field("said").bits), "count 0, said ");
  EXPECT_TRUE(heap_->Verify(&std::cerr));

  Click();
  EXPECT_EQ(rt::StringView(Field("said").bits), "count 1, said count 0, said ");
  EXPECT_TRUE(heap_->Verify(&std::cerr));
}

TEST_F(HeapWiringTest, WhatTheProgramMakesWhileItLoadsIsTenured) {
  // the string literals of the program: made by the module's initializer, which never moves them
  EXPECT_GT(heap_->Stats().tenured.allocations, 0U);
}

TEST(PhaseTest, TheHostMakesItsValuesInTheModel) {
  EXPECT_EQ(rt::GetPhase(), rt::Phase::kModel);
  const auto value = rt::MakeString("hello");
  EXPECT_EQ(kura::heap::Heap::Current()->SpaceOf(value.bits), kura::heap::Space::kModel);
  {
    const rt::PhaseScope frame(rt::Phase::kFrame);
    EXPECT_EQ(kura::heap::Heap::Current()->SpaceOf(rt::MakeString("x").bits), kura::heap::Space::kFrame);
    const rt::PhaseScope load(rt::Phase::kConstant);
    EXPECT_EQ(kura::heap::Heap::Current()->SpaceOf(rt::MakeString("y").bits), kura::heap::Space::kTenured);
  }
  EXPECT_EQ(rt::GetPhase(), rt::Phase::kModel);
}

TEST(PhaseTest, MovingOutOfTheFrameCopiesOnlyWhatIsThere) {
  const auto heap = kura::heap::Heap::Current();
  const auto model = rt::MakeString("model");
  rt::KuraValue frame{};
  {
    const rt::PhaseScope phase(rt::Phase::kFrame);
    frame = rt::MakeVariantWithTag(rt::VariantTag("Say"), rt::MakeString("frame"));
  }
  kura_rt_register_variant(rt::HashName("Say"), "Say");
  const auto moved = rt::MoveOutOfFrame(frame);
  EXPECT_EQ(moved.tag, frame.tag);
  EXPECT_NE(moved.bits, frame.bits);
  EXPECT_EQ(heap->SpaceOf(moved.bits), kura::heap::Space::kModel);
  EXPECT_EQ(rt::Format(moved), "Say(\"frame\")");
  // what isn't in the frame is the same object
  EXPECT_EQ(rt::MoveOutOfFrame(model).bits, model.bits);
  heap->ResetFrame();
  EXPECT_EQ(rt::Format(moved), "Say(\"frame\")");
}
}  // namespace
