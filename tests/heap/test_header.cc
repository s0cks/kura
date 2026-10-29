#include <gtest/gtest.h>

#include "heap/header.h"

namespace kura::heap {
TEST(ObjectHeaderTest, RoundTripsEveryField) {
  auto header = ObjectHeader::Make(ObjKind::kRecord, Space::kTenured, 4096);
  EXPECT_EQ(header.kind(), ObjKind::kRecord);
  EXPECT_EQ(header.space(), Space::kTenured);
  EXPECT_EQ(header.size(), 4096u);
  EXPECT_FALSE(header.marked());
  EXPECT_FALSE(header.forwarded());
  EXPECT_FALSE(header.remembered());
  EXPECT_EQ(header.age(), 0);
}

TEST(ObjectHeaderTest, FlagsAreIndependent) {
  auto header = ObjectHeader::Make(ObjKind::kSeq, Space::kModel, 64);
  header.set_marked();
  header.set_remembered();
  EXPECT_TRUE(header.marked());
  EXPECT_TRUE(header.remembered());
  EXPECT_FALSE(header.forwarded());
  header.set_marked(false);
  EXPECT_FALSE(header.marked());
  EXPECT_TRUE(header.remembered());
  // none of that touched the identity fields
  EXPECT_EQ(header.kind(), ObjKind::kSeq);
  EXPECT_EQ(header.space(), Space::kModel);
  EXPECT_EQ(header.size(), 64u);
}

TEST(ObjectHeaderTest, AgeSaturatesIntoItsField) {
  auto header = ObjectHeader::Make(ObjKind::kString, Space::kModel, 32);
  header.set_age(ObjectHeader::kMaxAge);
  EXPECT_EQ(header.age(), 15);
  EXPECT_EQ(header.size(), 32u);
  header.set_age(3);
  EXPECT_EQ(header.age(), 3);
}

TEST(ObjectHeaderTest, SetSizeKeepsEverythingElse) {
  auto header = ObjectHeader::Make(ObjKind::kFree, Space::kTenured, 32);
  header.set_marked();
  header.set_size(0xFFFFFFFFu);
  EXPECT_EQ(header.size(), 0xFFFFFFFFu);
  EXPECT_TRUE(header.IsFree());
  EXPECT_TRUE(header.marked());
  EXPECT_EQ(header.space(), Space::kTenured);
}

TEST(ObjectHeaderTest, ObjectSizeLeavesRoomForAForwardingAddress) {
  EXPECT_EQ(ObjectSizeFor(0), 16u);
  EXPECT_EQ(ObjectSizeFor(1), 16u);
  EXPECT_EQ(ObjectSizeFor(8), 16u);
  EXPECT_EQ(ObjectSizeFor(9), 24u);
  EXPECT_EQ(ObjectSizeFor(24), 32u);
  for (uword payload = 0; payload < 100; payload++) {
    EXPECT_EQ(ObjectSizeFor(payload) % kObjectAlignment, 0u);
    EXPECT_GE(ObjectSizeFor(payload), kMinBlockSize);
  }
}
}  // namespace kura::heap
