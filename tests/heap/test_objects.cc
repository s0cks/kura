#include <gtest/gtest.h>

#include <vector>

#include "heap/heap.h"
#include "heap/objects.h"

namespace kura::heap {
namespace {
auto SmallOptions() -> HeapOptions {
  HeapOptions options{};
  options.model_space_size = 64 * kKiB;
  options.tenured_size = 256 * kKiB;
  options.frame = {.chunk_size = 16 * kKiB, .max_size = 0};
  options.tick = {.chunk_size = 16 * kKiB, .max_size = 0};
  return options;
}

auto Ptr(const void* ptr) -> uint64_t {
  return reinterpret_cast<uint64_t>(ptr);
}
}  // namespace

TEST(ObjectsTest, StringsAreNulTerminatedForC) {
  Heap heap(SmallOptions());
  const auto str = NewString(heap, Space::kModel, "Hello World");
  ASSERT_NE(str, nullptr);
  EXPECT_EQ(str->kind(), ObjKind::kString);
  EXPECT_EQ(str->length, 11u);
  EXPECT_EQ(str->view(), "Hello World");
  EXPECT_STREQ(str->data(), "Hello World");
  EXPECT_EQ(str->size(), ObjectSizeFor(StringObj::PayloadSize(11)));
}

TEST(ObjectsTest, EmptyAndEmbeddedNulStrings) {
  Heap heap(SmallOptions());
  const auto empty = NewString(heap, Space::kModel, "");
  EXPECT_EQ(empty->length, 0u);
  EXPECT_STREQ(empty->data(), "");
  const std::string_view bytes("a\0b", 3);
  const auto str = NewString(heap, Space::kModel, bytes);
  EXPECT_EQ(str->view(), bytes);
  EXPECT_EQ(str->data()[3], '\0');
}

TEST(ObjectsTest, SeqsFillInUpToTheirCapacity) {
  Heap heap(SmallOptions());
  const auto seq = NewSeq(heap, Space::kModel, 3);
  ASSERT_NE(seq, nullptr);
  EXPECT_EQ(seq->length, 0u);
  EXPECT_EQ(seq->capacity, 3u);
  seq->Push({tag::kInt, 10});
  seq->Push({tag::kInt, 20});
  EXPECT_EQ(seq->length, 2u);
  EXPECT_EQ(seq->items()[1].bits, 20u);
  EXPECT_EQ(seq->size(), ObjectSizeFor(SeqObj::PayloadSize(3)));
  // the unfilled slot reads as an invalid, non-pointer value
  EXPECT_EQ(seq->items()[2].tag, tag::kInvalid);
}

TEST(ObjectsTest, AZeroCapacitySeqIsFine) {
  Heap heap(SmallOptions());
  const auto seq = NewSeq(heap, Space::kModel, 0);
  ASSERT_NE(seq, nullptr);
  EXPECT_EQ(seq->length, 0u);
  EXPECT_TRUE(heap.Verify());
}

TEST(ObjectsDeathTest, PushingPastCapacityIsCaught) {
  Heap heap(SmallOptions());
  const auto seq = NewSeq(heap, Space::kModel, 1);
  seq->Push({tag::kInt, 1});
  EXPECT_DEATH(seq->Push({tag::kInt, 2}), "is full");
}

TEST(ObjectsTest, RecordsKeepFieldOrderAndLaterWritesWin) {
  Heap heap(SmallOptions());
  const auto record = NewRecord(heap, Space::kModel, 3);
  ASSERT_NE(record, nullptr);
  EXPECT_TRUE(record->Set(111, "a", {tag::kInt, 1}));
  EXPECT_TRUE(record->Set(222, "b", {tag::kInt, 2}));
  EXPECT_TRUE(record->Set(111, "a", {tag::kInt, 99}));  // overwrite keeps position
  EXPECT_EQ(record->count, 2u);
  EXPECT_EQ(record->fields()[0].hash, 111u);
  EXPECT_EQ(record->fields()[0].value.bits, 99u);
  EXPECT_EQ(record->fields()[1].hash, 222u);
  ASSERT_NE(record->Find(222), nullptr);
  EXPECT_STREQ(record->Find(222)->name, "b");
  EXPECT_EQ(record->Find(333), nullptr);
}

TEST(ObjectsTest, AFullRecordRefusesNewFieldsButAcceptsOverwrites) {
  Heap heap(SmallOptions());
  const auto record = NewRecord(heap, Space::kModel, 1);
  EXPECT_TRUE(record->Set(1, "x", {tag::kInt, 1}));
  EXPECT_FALSE(record->Set(2, "y", {tag::kInt, 2}));
  EXPECT_TRUE(record->Set(1, "x", {tag::kInt, 3}));
  EXPECT_EQ(record->count, 1u);
  EXPECT_EQ(record->fields()[0].value.bits, 3u);
}

TEST(ObjectsTest, ClosuresStartWithNoneCaptures) {
  Heap heap(SmallOptions());
  int code = 0;
  const auto closure = NewClosure(heap, Space::kModel, &code, 2, 3);
  ASSERT_NE(closure, nullptr);
  EXPECT_EQ(closure->function, &code);
  EXPECT_EQ(closure->arity, 2u);
  EXPECT_EQ(closure->count, 3u);
  for (int idx = 0; idx < 3; idx++)
    EXPECT_EQ(closure->captures()[idx].tag, tag::kNone);
}

TEST(ObjectsTest, BoxesHoldAValue) {
  Heap heap(SmallOptions());
  const auto box = NewBox(heap, Space::kModel, {tag::kInt, 42});
  EXPECT_EQ(box->value.tag, tag::kInt);
  EXPECT_EQ(box->value.bits, 42u);
}

TEST(ObjectsTest, EveryObjectsRecordedSizeIsTheSizeItWasAllocatedWith) {
  Heap heap(SmallOptions());
  EXPECT_EQ(NewString(heap, Space::kModel, "abc")->size(), ObjectSizeFor(StringObj::PayloadSize(3)));
  EXPECT_EQ(NewRecord(heap, Space::kModel, 4)->size(), ObjectSizeFor(RecordObj::PayloadSize(4)));
  EXPECT_EQ(NewClosure(heap, Space::kModel, nullptr, 0, 2)->size(), ObjectSizeFor(ClosureObj::PayloadSize(2)));
  EXPECT_EQ(NewBox(heap, Space::kModel, {tag::kNone, 0})->size(), ObjectSizeFor(BoxObj::PayloadSize()));
  EXPECT_EQ(NewNode(heap, Space::kModel, "button")->size(), ObjectSizeFor(NodeObj::PayloadSize()));
  EXPECT_TRUE(heap.Verify());
}

TEST(ObjectsTest, LargeSeqsInTheTenuredSpaceAreRemembered) {
  Heap heap(SmallOptions());
  const auto seq = NewSeq(heap, Space::kTenured, 1000);
  ASSERT_NE(seq, nullptr);
  ASSERT_EQ(heap.remembered().size(), 1u);
  EXPECT_EQ(heap.remembered()[0], seq->address());
}

//--- VisitPointerSlots ------------------------------------------------------------------------------------------
TEST(VisitPointerSlotsTest, OnlyVisitsWordsThatMayBeHeapPointers) {
  Heap heap(SmallOptions());
  const auto str = NewString(heap, Space::kModel, "s");
  const auto inner = NewSeq(heap, Space::kModel, 0);
  const auto box = NewBox(heap, Space::kModel, {tag::kInt, 1});

  const auto seq = NewSeq(heap, Space::kModel, 10);
  seq->Push({tag::kInt, 7});                                    // a number: no
  seq->Push({tag::kString, Ptr(str)});                          // yes
  seq->Push({tag::kSeq, Ptr(inner)});                           // yes
  seq->Push({tag::kHandle, 0xDEADBEEF});                        // a host pointer: no
  seq->Push({tag::kFunction, 0});                               // null: no
  seq->Push({tag::kFloat, 0x4000000000000000ULL});              // a double's bits: no
  seq->Push({tag::kVariantBit | 0x1234, Ptr(box)});             // a variant's boxed payload: yes
  seq->Push({tag::kVariantBit | 0x1234, 0});                    // a bare variant: no
  seq->Push({tag::kColor, Ptr(str)});                           // yes

  std::vector<uint64_t> targets;
  VisitPointerSlots(seq, [&](uint64_t* slot) {
    targets.push_back(*slot);
  });
  EXPECT_EQ(targets, (std::vector<uint64_t>{Ptr(str), Ptr(inner), Ptr(box), Ptr(str)}));
}

TEST(VisitPointerSlotsTest, SlotsCanBeRewrittenInPlace) {
  Heap heap(SmallOptions());
  const auto from = NewString(heap, Space::kModel, "old");
  const auto to = NewString(heap, Space::kModel, "new");
  const auto record = NewRecord(heap, Space::kModel, 2);
  record->Set(1, "a", {tag::kString, Ptr(from)});
  record->Set(2, "b", {tag::kInt, 5});

  VisitPointerSlots(record, [&](uint64_t* slot) {
    if (*slot == Ptr(from))
      *slot = Ptr(to);
  });
  EXPECT_EQ(record->Find(1)->value.bits, Ptr(to));
  EXPECT_EQ(record->Find(2)->value.bits, 5u);
}

TEST(VisitPointerSlotsTest, CoversEveryPointerfulKind) {
  Heap heap(SmallOptions());
  const auto target = NewString(heap, Space::kModel, "t");
  const auto value = Value{tag::kString, Ptr(target)};
  const auto count = [](HeapObject* obj) {
    int visited = 0;
    VisitPointerSlots(obj, [&](uint64_t*) {
      visited++;
    });
    return visited;
  };

  const auto closure = NewClosure(heap, Space::kModel, nullptr, 0, 3);
  closure->captures()[0] = value;
  closure->captures()[2] = value;
  EXPECT_EQ(count(closure), 2);

  EXPECT_EQ(count(NewBox(heap, Space::kModel, value)), 1);
  EXPECT_EQ(count(NewBox(heap, Space::kModel, {tag::kBool, 1})), 0);
  EXPECT_EQ(count(target), 0);

  const auto node = NewNode(heap, Space::kModel, "button");
  EXPECT_EQ(count(node), 0);
  node->props = NewRecord(heap, Space::kModel, 1);
  node->children = NewSeq(heap, Space::kModel, 1);
  EXPECT_EQ(count(node), 2);
}

TEST(VisitPointerSlotsTest, HalfBuiltObjectsAreSafeToScan) {
  Heap heap(SmallOptions());
  const auto seq = NewSeq(heap, Space::kModel, 100);  // nothing pushed yet
  const auto record = NewRecord(heap, Space::kModel, 100);
  int visited = 0;
  VisitPointerSlots(seq, [&](uint64_t*) {
    visited++;
  });
  VisitPointerSlots(record, [&](uint64_t*) {
    visited++;
  });
  EXPECT_EQ(visited, 0);
}

TEST(TagTest, OnlyPointerCarryingTagsAreMarked) {
  EXPECT_TRUE(TagMayHoldHeapPointer(tag::kString));
  EXPECT_TRUE(TagMayHoldHeapPointer(tag::kColor));
  EXPECT_TRUE(TagMayHoldHeapPointer(tag::kSeq));
  EXPECT_TRUE(TagMayHoldHeapPointer(tag::kRecord));
  EXPECT_TRUE(TagMayHoldHeapPointer(tag::kNode));
  EXPECT_TRUE(TagMayHoldHeapPointer(tag::kFunction));
  EXPECT_TRUE(TagMayHoldHeapPointer(tag::kVariantBit | 42));
  for (const auto t : {tag::kInvalid, tag::kNone, tag::kBool, tag::kInt, tag::kFloat, tag::kPx, tag::kPct, tag::kFr,
                       tag::kHandle, tag::kVoid})
    EXPECT_FALSE(TagMayHoldHeapPointer(t)) << t;
}
}  // namespace kura::heap
