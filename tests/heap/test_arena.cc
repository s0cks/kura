#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "heap/arena.h"

namespace kura::heap {
namespace {
// Writes a header so ForEachObject can walk what was allocated.
void Stamp(const uword address, const uword size) {
  *reinterpret_cast<ObjectHeader*>(address) = ObjectHeader::Make(ObjKind::kBox, Space::kFrame, size);
}
}  // namespace

TEST(ArenaTest, AllocationsAreAlignedAndDistinct) {
  Arena arena(PageProvider::Default(), {.chunk_size = 4 * kKiB, .max_size = 0});
  const auto a = arena.TryAllocate(1);
  const auto b = arena.TryAllocate(9);
  const auto c = arena.TryAllocate(24);
  ASSERT_NE(a, kUnallocated);
  ASSERT_NE(b, kUnallocated);
  ASSERT_NE(c, kUnallocated);
  EXPECT_TRUE(IsAligned(a, kObjectAlignment));
  EXPECT_TRUE(IsAligned(b, kObjectAlignment));
  EXPECT_TRUE(IsAligned(c, kObjectAlignment));
  EXPECT_EQ(b, a + 8);
  EXPECT_EQ(c, b + 16);
  EXPECT_EQ(arena.used(), 8u + 16u + 24u);
  EXPECT_EQ(arena.allocations(), 3u);
}

TEST(ArenaTest, MemoryIsWritable) {
  Arena arena(PageProvider::Default(), {.chunk_size = 4 * kKiB, .max_size = 0});
  const auto address = arena.TryAllocate(256);
  ASSERT_NE(address, kUnallocated);
  std::memset(reinterpret_cast<void*>(address), 0xAB, 256);
  EXPECT_EQ(*reinterpret_cast<unsigned char*>(address + 255), 0xAB);
}

TEST(ArenaTest, GrowsByChunksWithoutMovingAnything) {
  Arena arena(PageProvider::Default(), {.chunk_size = 4 * kKiB, .max_size = 0});
  std::vector<uword> addresses;
  for (int idx = 0; idx < 10; idx++) {
    const auto address = arena.TryAllocate(1024);
    ASSERT_NE(address, kUnallocated);
    *reinterpret_cast<uint64_t*>(address) = 0x1000 + idx;
    addresses.push_back(address);
  }
  EXPECT_GT(arena.num_chunks(), 1u);
  for (int idx = 0; idx < 10; idx++)
    EXPECT_EQ(*reinterpret_cast<uint64_t*>(addresses[idx]), static_cast<uint64_t>(0x1000 + idx));
}

TEST(ArenaTest, ResetFoldsChunksIntoOne) {
  Arena arena(PageProvider::Default(), {.chunk_size = 4 * kKiB, .max_size = 0});
  for (int idx = 0; idx < 10; idx++)
    ASSERT_NE(arena.TryAllocate(1024), kUnallocated);
  ASSERT_GT(arena.num_chunks(), 1u);
  const auto peak = arena.used();

  arena.Reset();
  EXPECT_EQ(arena.used(), 0u);
  EXPECT_EQ(arena.num_chunks(), 1u);
  EXPECT_GE(arena.reserved(), peak);
  EXPECT_EQ(arena.high_water(), peak);
  EXPECT_EQ(arena.resets(), 1u);

  // the same workload now fits in the one chunk
  for (int idx = 0; idx < 10; idx++)
    ASSERT_NE(arena.TryAllocate(1024), kUnallocated);
  EXPECT_EQ(arena.num_chunks(), 1u);
}

TEST(ArenaTest, ResetReusesTheSameMemory) {
  Arena arena(PageProvider::Default(), {.chunk_size = 4 * kKiB, .max_size = 0});
  const auto first = arena.TryAllocate(64);
  arena.Reset();
  EXPECT_EQ(arena.TryAllocate(64), first);
}

TEST(ArenaTest, ARequestBiggerThanAChunkGetsItsOwn) {
  Arena arena(PageProvider::Default(), {.chunk_size = 4 * kKiB, .max_size = 0});
  const auto big = arena.TryAllocate(100 * kKiB);
  ASSERT_NE(big, kUnallocated);
  std::memset(reinterpret_cast<void*>(big), 1, 100 * kKiB);
  EXPECT_GE(arena.reserved(), 100 * kKiB);
}

TEST(ArenaTest, TheCapIsAHardBudget) {
  Arena arena(PageProvider::Default(), {.chunk_size = 4 * kKiB, .max_size = 8 * kKiB});
  uword total = 0;
  while (arena.TryAllocate(512) != kUnallocated)
    total += 512;
  EXPECT_LE(total, 8 * kKiB);
  EXPECT_GE(total, 4 * kKiB);
  EXPECT_EQ(arena.TryAllocate(512), kUnallocated);
  EXPECT_EQ(arena.TryAllocate(1 * kMiB), kUnallocated);

  // a reset gives the budget back
  arena.Reset();
  EXPECT_NE(arena.TryAllocate(512), kUnallocated);
}

TEST(ArenaTest, ContainsOnlyWhatWasAllocated) {
  Arena arena(PageProvider::Default(), {.chunk_size = 4 * kKiB, .max_size = 0});
  const auto address = arena.TryAllocate(64);
  EXPECT_TRUE(arena.Contains(address));
  EXPECT_TRUE(arena.Contains(address + 63));
  EXPECT_FALSE(arena.Contains(address + 64));
  EXPECT_FALSE(arena.Contains(0));
  int local = 0;
  EXPECT_FALSE(arena.Contains(reinterpret_cast<uword>(&local)));
  arena.Reset();
  EXPECT_FALSE(arena.Contains(address));
}

TEST(ArenaTest, ForEachObjectWalksInAllocationOrderAcrossChunks) {
  Arena arena(PageProvider::Default(), {.chunk_size = 4 * kKiB, .max_size = 0});
  std::vector<uword> expected;
  for (int idx = 0; idx < 12; idx++) {
    const auto size = 16 + 8 * (idx % 3);
    const auto address = arena.TryAllocate(size);
    ASSERT_NE(address, kUnallocated);
    Stamp(address, size);
    expected.push_back(address);
    // pad so we cross chunk boundaries
    const auto pad = arena.TryAllocate(1000);
    ASSERT_NE(pad, kUnallocated);
    Stamp(pad, 1000);
    expected.push_back(pad);
  }
  ASSERT_GT(arena.num_chunks(), 1u);
  std::vector<uword> seen;
  arena.ForEachObject([&](const uword address) {
    seen.push_back(address);
  });
  EXPECT_EQ(seen, expected);
}

#ifdef KURA_HEAP_DEBUG_ENABLED
TEST(ArenaTest, ResetPoisonsInDebugBuilds) {
  Arena arena(PageProvider::Default(), {.chunk_size = 4 * kKiB, .max_size = 0});
  const auto address = arena.TryAllocate(64);
  std::memset(reinterpret_cast<void*>(address), 0x11, 64);
  arena.Reset();
  EXPECT_EQ(*reinterpret_cast<unsigned char*>(address), 0xDD);
}
#endif  // KURA_HEAP_DEBUG_ENABLED
}  // namespace kura::heap
