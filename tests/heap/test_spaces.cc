#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "heap/semispace.h"
#include "heap/tenured_space.h"

namespace kura::heap {
namespace {
void Stamp(const uword address, const uword size, const ObjKind kind = ObjKind::kBox,
           const Space space = Space::kModel) {
  *reinterpret_cast<ObjectHeader*>(address) = ObjectHeader::Make(kind, space, size);
}
}  // namespace

//--- ModelSpace ---------------------------------------------------------------------------------------------------
TEST(ModelSpaceTest, SplitsItsReservationIntoTwoEqualHalves) {
  ModelSpace space(PageProvider::Default(), 64 * kKiB);
  EXPECT_EQ(space.capacity(), 32 * kKiB);
  EXPECT_EQ(space.from_space().size(), space.to_space().size());
  EXPECT_EQ(space.from_space().end(), space.to_space().start());
  EXPECT_EQ(space.used(), 0u);
}

TEST(ModelSpaceTest, BumpsThroughTheFromSpaceUntilItIsFull) {
  ModelSpace space(PageProvider::Default(), 64 * kKiB);
  const auto first = space.TryAllocate(24);
  const auto second = space.TryAllocate(16);
  EXPECT_EQ(first, space.from_space().start());
  EXPECT_EQ(second, first + 24);
  EXPECT_TRUE(space.ContainsInFromSpace(first));
  EXPECT_FALSE(space.ContainsInToSpace(first));

  uword count = 2;
  while (space.TryAllocate(1024) != kUnallocated)
    count++;
  // exactly one half's worth, never a byte into the other half
  EXPECT_EQ(space.used(), 24u + 16u + (count - 2) * 1024u);
  EXPECT_LE(space.used(), space.capacity());
  EXPECT_LT(space.remaining(), 1024u);
  EXPECT_EQ(space.TryAllocate(space.capacity()), kUnallocated);
}

TEST(ModelSpaceTest, CopiesGoToTheOtherHalfAndFlipPromotesThem) {
  ModelSpace space(PageProvider::Default(), 64 * kKiB);
  const auto old_from = space.from_space();
  const auto old_to = space.to_space();

  // some objects, one of which "survives"
  const auto a = space.TryAllocate(32);
  const auto dead = space.TryAllocate(32);
  Stamp(a, 32);
  Stamp(dead, 32);
  (void)dead;

  const auto copy = space.TryAllocateInToSpace(32);
  ASSERT_EQ(copy, old_to.start());
  EXPECT_TRUE(space.ContainsInToSpace(copy));
  std::memcpy(reinterpret_cast<void*>(copy), reinterpret_cast<void*>(a), 32);

  space.Flip();
  EXPECT_EQ(space.flips(), 1u);
  EXPECT_EQ(space.from_space().start(), old_to.start());
  EXPECT_EQ(space.to_space().start(), old_from.start());
  EXPECT_EQ(space.used(), 32u);
  EXPECT_TRUE(space.ContainsInFromSpace(copy));

  // the new to-space starts empty
  EXPECT_EQ(space.TryAllocateInToSpace(8), old_from.start());

  // and the survivors are all that a walk sees
  std::vector<uword> seen;
  space.ForEachObject([&](const uword address) {
    seen.push_back(address);
  });
  ASSERT_EQ(seen.size(), 1u);
  EXPECT_EQ(seen[0], copy);

  // allocation continues after the survivors
  EXPECT_EQ(space.TryAllocate(16), copy + 32);
}

TEST(ModelSpaceTest, ToSpaceHasTheSameCapacityAsFromSpace) {
  ModelSpace space(PageProvider::Default(), 64 * kKiB);
  uword copied = 0;
  while (space.TryAllocateInToSpace(1024) != kUnallocated)
    copied += 1024;
  EXPECT_EQ(copied, space.capacity() - space.capacity() % 1024);
}

TEST(ModelSpaceTest, ContainsCoversBothHalvesAndNothingElse) {
  ModelSpace space(PageProvider::Default(), 64 * kKiB);
  EXPECT_TRUE(space.Contains(space.from_space().start()));
  EXPECT_TRUE(space.Contains(space.to_space().start() + 10));
  EXPECT_FALSE(space.Contains(0));
  int local = 0;
  EXPECT_FALSE(space.Contains(reinterpret_cast<uword>(&local)));
}

TEST(ModelSpaceTest, ResetEmptiesBothHalves) {
  ModelSpace space(PageProvider::Default(), 64 * kKiB);
  space.TryAllocate(128);
  space.TryAllocateInToSpace(128);
  space.Reset();
  EXPECT_EQ(space.used(), 0u);
  EXPECT_EQ(space.TryAllocateInToSpace(8), space.to_space().start());
  EXPECT_EQ(space.high_water(), 128u);
}

//--- FreeList / TenuredSpace ------------------------------------------------------------------------------------
TEST(TenuredSpaceTest, StartsAsOneFreeBlock) {
  TenuredSpace space(PageProvider::Default(), 256 * kKiB);
  EXPECT_EQ(space.used(), 0u);
  EXPECT_EQ(space.free_list().num_blocks(), 1u);
  EXPECT_EQ(space.free_list().largest_block(), space.reserved());
}

TEST(TenuredSpaceTest, AllocatesFromTheFrontAndSplits) {
  TenuredSpace space(PageProvider::Default(), 256 * kKiB);
  uword actual = 0;
  const auto a = space.TryAllocate(1000, &actual);
  EXPECT_EQ(actual, 1000u);
  const auto b = space.TryAllocate(104, &actual);
  EXPECT_EQ(b, a + 1000);
  EXPECT_EQ(space.used(), 1104u);
  EXPECT_EQ(space.free_list().num_blocks(), 1u);
  EXPECT_TRUE(space.Contains(a));
}

TEST(TenuredSpaceTest, SmallRequestsAreRoundedUpToAWholeBlock) {
  TenuredSpace space(PageProvider::Default(), 256 * kKiB);
  uword actual = 0;
  ASSERT_NE(space.TryAllocate(1, &actual), kUnallocated);
  EXPECT_EQ(actual, kMinBlockSize);
}

TEST(TenuredSpaceTest, BestFitTakesTheSmallestHoleThatFits) {
  TenuredSpace space(PageProvider::Default(), 256 * kKiB);
  uword n = 0;
  const auto a = space.TryAllocate(1000, &n);
  const auto b = space.TryAllocate(104, &n);
  const auto c = space.TryAllocate(504, &n);
  const auto d = space.TryAllocate(104, &n);
  (void)b;
  (void)d;
  space.Free(a, 1000);
  space.Free(c, 504);
  ASSERT_EQ(space.free_list().num_blocks(), 3u);  // two holes and the tail

  // 400 fits both holes; the 504-byte one is the smaller
  uword actual = 0;
  const auto e = space.TryAllocate(400, &actual);
  EXPECT_EQ(e, c);
  EXPECT_EQ(actual, 400u);
  // the 104 bytes left over stay on the list, in place
  EXPECT_EQ(space.free_list().num_blocks(), 3u);

  // 104 now fits that leftover exactly
  const auto f = space.TryAllocate(104, &actual);
  EXPECT_EQ(f, c + 400);
  EXPECT_EQ(space.free_list().num_blocks(), 2u);
}

TEST(TenuredSpaceTest, AnAlmostExactFitTakesTheWholeBlock) {
  TenuredSpace space(PageProvider::Default(), 256 * kKiB);
  uword n = 0;
  const auto a = space.TryAllocate(64, &n);
  const auto guard = space.TryAllocate(64, &n);
  (void)guard;
  space.Free(a, 64);

  // 56 leaves 8 bytes, too small to be a block: the caller gets all 64
  uword actual = 0;
  EXPECT_EQ(space.TryAllocate(56, &actual), a);
  EXPECT_EQ(actual, 64u);
}

TEST(TenuredSpaceTest, FreeingEverythingCoalescesBackToOneBlock) {
  TenuredSpace space(PageProvider::Default(), 256 * kKiB);
  std::vector<std::pair<uword, uword>> blocks;
  for (int idx = 0; idx < 50; idx++) {
    uword actual = 0;
    const auto address = space.TryAllocate(64 + 8 * (idx % 7), &actual);
    ASSERT_NE(address, kUnallocated);
    blocks.emplace_back(address, actual);
  }
  // free in a scrambled order: evens, then odds in reverse
  for (size_t idx = 0; idx < blocks.size(); idx += 2)
    space.Free(blocks[idx].first, blocks[idx].second);
  EXPECT_GT(space.free_list().num_blocks(), 20u);
  for (size_t idx = blocks.size(); idx-- > 0;) {
    if (idx % 2 == 1)
      space.Free(blocks[idx].first, blocks[idx].second);
  }

  EXPECT_EQ(space.used(), 0u);
  EXPECT_EQ(space.free_list().num_blocks(), 1u);
  EXPECT_EQ(space.free_list().largest_block(), space.reserved());
}

TEST(TenuredSpaceTest, FailsCleanlyWhenFull) {
  TenuredSpace space(PageProvider::Default(), 64 * kKiB);
  uword actual = 0;
  while (space.TryAllocate(1024, &actual) != kUnallocated) {
  }
  EXPECT_EQ(space.TryAllocate(space.reserved(), &actual), kUnallocated);
  EXPECT_LT(space.reserved() - space.used(), 1024u);
}

TEST(TenuredSpaceTest, ForEachObjectSkipsFreeBlocks) {
  TenuredSpace space(PageProvider::Default(), 256 * kKiB);
  std::vector<uword> live;
  uword actual = 0;
  uword freed = 0;
  for (int idx = 0; idx < 6; idx++) {
    const auto address = space.TryAllocate(64, &actual);
    Stamp(address, actual, ObjKind::kString, Space::kTenured);
    if (idx == 2)
      freed = address;
    else
      live.push_back(address);
  }
  space.Free(freed, 64);

  std::vector<uword> seen;
  space.ForEachObject([&](const uword address) {
    seen.push_back(address);
  });
  EXPECT_EQ(seen, live);
}

TEST(TenuredSpaceDeathTest, DoubleFreeIsCaught) {
  TenuredSpace space(PageProvider::Default(), 64 * kKiB);
  uword actual = 0;
  const auto address = space.TryAllocate(64, &actual);
  space.Free(address, 64);
  EXPECT_DEATH(space.Free(address, 64), "overlaps");
}
}  // namespace kura::heap
