#include <gtest/gtest.h>

#include <cstring>
#include <sstream>
#include <thread>

#include "heap/heap.h"

namespace kura::heap {
namespace {
auto SmallOptions() -> HeapOptions {
  HeapOptions options{};
  options.model_space_size = 64 * kKiB;
  options.tenured_size = 256 * kKiB;
  options.large_object_threshold = 4 * kKiB;
  options.frame = {.chunk_size = 16 * kKiB, .max_size = 0};
  options.tick = {.chunk_size = 16 * kKiB, .max_size = 0};
  options.permanent = {.chunk_size = 16 * kKiB, .max_size = 0};
  return options;
}

struct OomRecorder {
  static inline int calls = 0;
  static inline Space space = Space::kStatic;
  static inline uword requested = 0;
  static void Handle(const Heap&, const Space s, const uword n) {
    calls++;
    space = s;
    requested = n;
  }
  static void Reset() {
    calls = 0;
    space = Space::kStatic;
    requested = 0;
  }
};
}  // namespace

TEST(HeapTest, ObjectsGetAHeaderAndAZeroedPayload) {
  Heap heap(SmallOptions());
  const auto obj = heap.AllocateObject(Space::kFrame, ObjKind::kSeq, 40);
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(obj->kind(), ObjKind::kSeq);
  EXPECT_EQ(obj->space(), Space::kFrame);
  EXPECT_EQ(obj->size(), ObjectSizeFor(40));
  EXPECT_TRUE(IsAligned(obj->address(), kObjectAlignment));
  const auto payload = reinterpret_cast<const unsigned char*>(obj) + kHeaderSize;
  for (uword idx = 0; idx < obj->size() - kHeaderSize; idx++)
    ASSERT_EQ(payload[idx], 0) << "byte " << idx;
}

TEST(HeapTest, PayloadsAreZeroedEvenWhenTheMemoryIsReused) {
  Heap heap(SmallOptions());
  const auto first = heap.AllocateObject(Space::kTick, ObjKind::kBox, 16);
  std::memset(reinterpret_cast<unsigned char*>(first) + kHeaderSize, 0xFF, 16);
  heap.ResetTick();
  const auto second = heap.AllocateObject(Space::kTick, ObjKind::kBox, 16);
  EXPECT_EQ(first, second);
  const auto payload = reinterpret_cast<const uint64_t*>(reinterpret_cast<const unsigned char*>(second) + kHeaderSize);
  EXPECT_EQ(payload[0], 0u);
  EXPECT_EQ(payload[1], 0u);
}

TEST(HeapTest, EachSpaceHandsOutMemoryInItsOwnRegion) {
  Heap heap(SmallOptions());
  const auto frame = heap.AllocateObject(Space::kFrame, ObjKind::kString, 8);
  const auto tick = heap.AllocateObject(Space::kTick, ObjKind::kString, 8);
  const auto model = heap.AllocateObject(Space::kModel, ObjKind::kString, 8);
  const auto tenured = heap.AllocateObject(Space::kTenured, ObjKind::kString, 8);
  EXPECT_EQ(heap.SpaceOf(frame->address()), Space::kFrame);
  EXPECT_EQ(heap.SpaceOf(tick->address()), Space::kTick);
  EXPECT_EQ(heap.SpaceOf(model->address()), Space::kModel);
  EXPECT_EQ(heap.SpaceOf(tenured->address()), Space::kTenured);
  EXPECT_EQ(model->space(), Space::kModel);
  EXPECT_EQ(tenured->space(), Space::kTenured);
}

TEST(HeapTest, AddressesTheHeapDoesNotManageAreStatic) {
  Heap heap(SmallOptions());
  static const int literal = 0;
  int local = 0;
  EXPECT_EQ(heap.SpaceOf(0), Space::kStatic);
  EXPECT_EQ(heap.SpaceOf(reinterpret_cast<uword>(&literal)), Space::kStatic);
  EXPECT_EQ(heap.SpaceOf(reinterpret_cast<uword>(&local)), Space::kStatic);
  EXPECT_FALSE(heap.Contains(0));
}

TEST(HeapTest, AllocateSendsLargeObjectsStraightToTheTenuredSpace) {
  Heap heap(SmallOptions());
  const auto small = heap.Allocate(ObjKind::kSeq, 100);
  const auto large = heap.Allocate(ObjKind::kSeq, 8 * kKiB);
  const auto edge = heap.Allocate(ObjKind::kString, 4 * kKiB - kHeaderSize);  // exactly the threshold
  EXPECT_EQ(small->space(), Space::kModel);
  EXPECT_EQ(large->space(), Space::kTenured);
  EXPECT_EQ(edge->space(), Space::kTenured);
}

TEST(HeapTest, TenuredObjectsThatCanHoldPointersAreRemembered) {
  Heap heap(SmallOptions());
  const auto seq = heap.AllocateObject(Space::kTenured, ObjKind::kSeq, 256);
  const auto str = heap.AllocateObject(Space::kTenured, ObjKind::kString, 256);
  const auto model_seq = heap.AllocateObject(Space::kModel, ObjKind::kSeq, 256);
  ASSERT_EQ(heap.remembered().size(), 1u);
  EXPECT_EQ(heap.remembered()[0], seq->address());
  EXPECT_TRUE(seq->header.remembered());
  EXPECT_FALSE(str->header.remembered());
  EXPECT_FALSE(model_seq->header.remembered());
  heap.ClearRemembered();
  EXPECT_TRUE(heap.remembered().empty());
}

TEST(HeapTest, AFullModelSpaceOverflowsIntoTheTenuredSpace) {
  Heap heap(SmallOptions());
  EXPECT_FALSE(heap.NeedsCollection());
  int in_model = 0;
  int in_tenured = 0;
  for (int idx = 0; idx < 2000; idx++) {
    const auto obj = heap.AllocateModel(ObjKind::kBox, 16);
    ASSERT_NE(obj, nullptr);
    (obj->space() == Space::kModel ? in_model : in_tenured)++;
    EXPECT_EQ(heap.SpaceOf(obj->address()), obj->space());
  }
  EXPECT_GT(in_model, 0);
  EXPECT_GT(in_tenured, 0);
  const auto stats = heap.Stats();
  EXPECT_EQ(stats.model_overflow_allocations, static_cast<uword>(in_tenured));
  EXPECT_GE(stats.model_overflow_bytes, static_cast<uword>(in_tenured) * ObjectSizeFor(16));
  EXPECT_TRUE(heap.NeedsCollection());
  EXPECT_TRUE(heap.Verify());
}

TEST(HeapTest, NeedsCollectionWatchesTheModelSpaceFillingUp) {
  Heap heap(SmallOptions());
  while (heap.model().used() * 100 < heap.model().capacity() * 70)
    heap.AllocateModel(ObjKind::kBox, 16);
  EXPECT_FALSE(heap.NeedsCollection(75));
  EXPECT_TRUE(heap.NeedsCollection(50));
}

TEST(HeapTest, FrameAndTickResetIndependently) {
  Heap heap(SmallOptions());
  heap.AllocateObject(Space::kFrame, ObjKind::kBox, 16);
  heap.AllocateObject(Space::kTick, ObjKind::kBox, 16);
  heap.ResetFrame();
  EXPECT_EQ(heap.Stats().frame.used, 0u);
  EXPECT_EQ(heap.Stats().tick.used, ObjectSizeFor(16));
  heap.ResetTick();
  EXPECT_EQ(heap.Stats().tick.used, 0u);
  EXPECT_EQ(heap.Stats().frame.high_water, ObjectSizeFor(16));
}

TEST(HeapTest, AFrameBudgetIsEnforcedWithoutTouchingTheOtherSpaces) {
  auto options = SmallOptions();
  options.frame = {.chunk_size = 4 * kKiB, .max_size = 4 * kKiB};
  Heap heap(options);
  OomRecorder::Reset();
  heap.SetOutOfMemoryHandler(&OomRecorder::Handle);

  int ok = 0;
  while (heap.AllocateObject(Space::kFrame, ObjKind::kBox, 240) != nullptr)
    ok++;
  EXPECT_GT(ok, 0);
  EXPECT_EQ(OomRecorder::calls, 1);
  EXPECT_EQ(OomRecorder::space, Space::kFrame);
  EXPECT_EQ(OomRecorder::requested, ObjectSizeFor(240));
  EXPECT_NE(heap.AllocateObject(Space::kTick, ObjKind::kBox, 240), nullptr);
  EXPECT_NE(heap.AllocateObject(Space::kModel, ObjKind::kBox, 240), nullptr);

  heap.ResetFrame();
  EXPECT_NE(heap.AllocateObject(Space::kFrame, ObjKind::kBox, 240), nullptr);
}

TEST(HeapTest, ExhaustingTheTenuredSpaceReportsOutOfMemory) {
  Heap heap(SmallOptions());
  OomRecorder::Reset();
  heap.SetOutOfMemoryHandler(&OomRecorder::Handle);
  int ok = 0;
  while (heap.AllocateObject(Space::kTenured, ObjKind::kString, 8 * kKiB) != nullptr)
    ok++;
  EXPECT_GE(ok, 20);
  EXPECT_EQ(OomRecorder::calls, 1);
  EXPECT_EQ(OomRecorder::space, Space::kTenured);
}

TEST(HeapDeathTest, TheDefaultHandlerAborts) {
  auto options = SmallOptions();
  options.frame = {.chunk_size = 4 * kKiB, .max_size = 4 * kKiB};
  Heap heap(options);
  EXPECT_DEATH(
      {
        for (int idx = 0; idx < 100; idx++)
          heap.AllocateObject(Space::kFrame, ObjKind::kBox, 240);
      },
      "out of memory: cannot allocate");
}

TEST(HeapTest, PermanentMemoryIsAlignedAndStable) {
  Heap heap(SmallOptions());
  const auto a = heap.AllocatePermanent(100, 64);
  const auto b = heap.AllocatePermanent(100, 64);
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  EXPECT_TRUE(IsAligned(reinterpret_cast<uword>(a), 64));
  EXPECT_TRUE(IsAligned(reinterpret_cast<uword>(b), 64));
  EXPECT_NE(a, b);
  std::memset(a, 7, 100);
  heap.ResetFrame();
  heap.ResetTick();
  EXPECT_EQ(static_cast<unsigned char*>(a)[99], 7);
  // permanent memory isn't an object space
  EXPECT_EQ(heap.SpaceOf(reinterpret_cast<uword>(a)), Space::kStatic);
}

TEST(HeapTest, StatsTrackEachSpace) {
  Heap heap(SmallOptions());
  heap.AllocateObject(Space::kFrame, ObjKind::kBox, 16);
  heap.AllocateObject(Space::kFrame, ObjKind::kBox, 16);
  heap.AllocateObject(Space::kModel, ObjKind::kBox, 16);
  heap.AllocateObject(Space::kTenured, ObjKind::kBox, 16);
  const auto stats = heap.Stats();
  EXPECT_EQ(stats.frame.allocations, 2u);
  EXPECT_EQ(stats.frame.used, 2 * ObjectSizeFor(16));
  EXPECT_EQ(stats.model.allocations, 1u);
  EXPECT_EQ(stats.model.used, ObjectSizeFor(16));
  EXPECT_EQ(stats.tenured.allocations, 1u);
  EXPECT_EQ(stats.tenured.used, ObjectSizeFor(16));
  EXPECT_EQ(stats.model_overflow_allocations, 0u);

  std::ostringstream out;
  heap.Dump(out);
  EXPECT_NE(out.str().find("model"), std::string::npos);
}

TEST(HeapTest, ForEachObjectSeesEverythingInASpace) {
  Heap heap(SmallOptions());
  for (int idx = 0; idx < 5; idx++)
    heap.AllocateObject(Space::kModel, ObjKind::kBox, 16);
  for (int idx = 0; idx < 3; idx++)
    heap.AllocateObject(Space::kTenured, ObjKind::kString, 100);
  int model = 0;
  int tenured = 0;
  heap.ForEachObject(Space::kModel, [&](HeapObject*) {
    model++;
  });
  heap.ForEachObject(Space::kTenured, [&](HeapObject*) {
    tenured++;
  });
  EXPECT_EQ(model, 5);
  EXPECT_EQ(tenured, 3);
}

TEST(HeapTest, TheCurrentHeapIsPerThread) {
  Heap heap(SmallOptions());
  EXPECT_EQ(Heap::Current(), nullptr);
  Heap::SetCurrent(&heap);
  EXPECT_EQ(Heap::Current(), &heap);
  Heap* seen = &heap;
  std::thread([&] {
    seen = Heap::Current();
  }).join();
  EXPECT_EQ(seen, nullptr);
  Heap::SetCurrent(nullptr);
}

TEST(HeapTest, DestroyingTheCurrentHeapClearsIt) {
  {
    Heap heap(SmallOptions());
    Heap::SetCurrent(&heap);
  }
  EXPECT_EQ(Heap::Current(), nullptr);
}

//--- Verify -----------------------------------------------------------------------------------------------------
TEST(HeapVerifyTest, AHealthyHeapVerifies) {
  Heap heap(SmallOptions());
  for (int idx = 0; idx < 20; idx++) {
    NewString(heap, Space::kFrame, "frame");
    NewString(heap, Space::kTick, "tick");
    NewString(heap, Space::kModel, "model");
    NewSeq(heap, Space::kTenured, 100);
  }
  std::ostringstream out;
  EXPECT_TRUE(heap.Verify(&out)) << out.str();
}

TEST(HeapVerifyTest, CatchesAModelObjectPointingIntoTheFrame) {
  Heap heap(SmallOptions());
  const auto scratch = NewString(heap, Space::kFrame, "temporary");
  const auto seq = NewSeq(heap, Space::kModel, 2);
  seq->Push({tag::kString, reinterpret_cast<uint64_t>(scratch)});

  std::ostringstream out;
  EXPECT_FALSE(heap.Verify(&out));
  EXPECT_NE(out.str().find("points into the frame space"), std::string::npos) << out.str();

  // pointing at something that lives as long as the model is fine
  const auto durable = NewString(heap, Space::kModel, "durable");
  seq->items()[0] = {tag::kString, reinterpret_cast<uint64_t>(durable)};
  EXPECT_TRUE(heap.Verify());
}

TEST(HeapVerifyTest, CatchesATenuredObjectPointingIntoTheTick) {
  Heap heap(SmallOptions());
  const auto scratch = NewString(heap, Space::kTick, "message");
  const auto box = NewBox(heap, Space::kTenured, {tag::kString, reinterpret_cast<uint64_t>(scratch)});
  ASSERT_NE(box, nullptr);
  std::ostringstream out;
  EXPECT_FALSE(heap.Verify(&out));
  EXPECT_NE(out.str().find("points into the tick space"), std::string::npos) << out.str();
}

TEST(HeapVerifyTest, ScratchMayPointAnywhere) {
  Heap heap(SmallOptions());
  const auto model_str = NewString(heap, Space::kModel, "m");
  const auto frame_seq = NewSeq(heap, Space::kFrame, 2);
  frame_seq->Push({tag::kString, reinterpret_cast<uint64_t>(model_str)});
  const auto frame_str = NewString(heap, Space::kFrame, "f");
  frame_seq->Push({tag::kString, reinterpret_cast<uint64_t>(frame_str)});
  EXPECT_TRUE(heap.Verify());
}

TEST(HeapVerifyTest, CatchesACorruptKind) {
  Heap heap(SmallOptions());
  const auto str = NewString(heap, Space::kModel, "oops");
  str->header = ObjectHeader::Make(static_cast<ObjKind>(77), Space::kModel, str->size());
  std::ostringstream out;
  EXPECT_FALSE(heap.Verify(&out));
  EXPECT_NE(out.str().find("invalid kind"), std::string::npos) << out.str();
}

TEST(HeapVerifyTest, CatchesAWrongSpaceInTheHeader) {
  Heap heap(SmallOptions());
  const auto str = NewString(heap, Space::kModel, "oops");
  str->header = ObjectHeader::Make(ObjKind::kString, Space::kTenured, str->size());
  std::ostringstream out;
  EXPECT_FALSE(heap.Verify(&out));
  EXPECT_NE(out.str().find("wrong space"), std::string::npos) << out.str();
}

TEST(HeapVerifyTest, CatchesAnObjectLeftForwarded) {
  Heap heap(SmallOptions());
  const auto str = NewString(heap, Space::kModel, "oops");
  str->header.set_forwarded();
  EXPECT_FALSE(heap.Verify());
}
}  // namespace kura::heap
