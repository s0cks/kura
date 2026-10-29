#include "heap/free_list.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace kura::heap {
void FreeList::Init(const Region& region) {
  KURA_HEAP_CHECK(region.size() >= kMinBlockSize && IsAligned(region.start(), kObjectAlignment));
  region_ = region;
  const auto size = RoundDown(region.size(), kObjectAlignment);
  head_ = new (reinterpret_cast<void*>(region.start()))
      FreeBlock{ObjectHeader::Make(ObjKind::kFree, Space::kTenured, size), nullptr};
  free_bytes_ = size;
}

auto FreeList::TryAllocate(uword size, uword* actual) -> uword {
  size = RoundUp(std::max(size, kMinBlockSize), kObjectAlignment);

  FreeBlock* best = nullptr;
  FreeBlock* best_prev = nullptr;
  FreeBlock* prev = nullptr;
  for (auto* block = head_; block != nullptr; prev = block, block = block->next) {
    const auto block_size = block->header.size();
    if (block_size < size)
      continue;
    if (best == nullptr || block_size < best->header.size()) {
      best = block;
      best_prev = prev;
      if (block_size == size)
        break;  // can't do better than an exact fit
    }
  }
  if (best == nullptr)
    return kUnallocated;

  const auto address = reinterpret_cast<uword>(best);
  const auto block_size = best->header.size();
  const auto remainder = block_size - size;
  uword taken = block_size;
  if (remainder >= kMinBlockSize) {
    // Keep the front for the caller, leave the tail on the list in the same place.
    auto* tail = new (reinterpret_cast<void*>(address + size))
        FreeBlock{ObjectHeader::Make(ObjKind::kFree, Space::kTenured, remainder), best->next};
    (best_prev ? best_prev->next : head_) = tail;
    taken = size;
  } else {
    (best_prev ? best_prev->next : head_) = best->next;
  }
  free_bytes_ -= taken;
  if (actual)
    *actual = taken;
  return address;
}

void FreeList::Free(const uword address, uword size) {
  size = RoundUp(std::max(size, kMinBlockSize), kObjectAlignment);
  KURA_HEAP_CHECK(region_.Contains(address) && address + size <= region_.end(), "(free of %p is outside the space)",
                  reinterpret_cast<void*>(address));
  KURA_HEAP_CHECK(IsAligned(address, kObjectAlignment));

  // Find the neighbours in address order.
  FreeBlock* prev = nullptr;
  FreeBlock* next = head_;
  while (next != nullptr && reinterpret_cast<uword>(next) < address) {
    prev = next;
    next = next->next;
  }
  KURA_HEAP_CHECK(prev == nullptr || reinterpret_cast<uword>(prev) + prev->header.size() <= address,
                  "(free of %p overlaps a free block)", reinterpret_cast<void*>(address));
  KURA_HEAP_CHECK(next == nullptr || address + size <= reinterpret_cast<uword>(next),
                  "(free of %p overlaps a free block)", reinterpret_cast<void*>(address));

#ifdef KURA_HEAP_DEBUG_ENABLED
  std::memset(reinterpret_cast<void*>(address), 0xDD, size);
#endif  // KURA_HEAP_DEBUG_ENABLED

  auto* block = new (reinterpret_cast<void*>(address))
      FreeBlock{ObjectHeader::Make(ObjKind::kFree, Space::kTenured, size), next};
  if (prev)
    prev->next = block;
  else
    head_ = block;
  free_bytes_ += size;

  // Merge with the block after, then the block before.
  if (next != nullptr && address + block->header.size() == reinterpret_cast<uword>(next)) {
    block->header.set_size(block->header.size() + next->header.size());
    block->next = next->next;
  }
  if (prev != nullptr && reinterpret_cast<uword>(prev) + prev->header.size() == address) {
    prev->header.set_size(prev->header.size() + block->header.size());
    prev->next = block->next;
  }
}

auto FreeList::num_blocks() const -> uword {
  uword count = 0;
  ForEachBlock([&](uword, uword) { count++; });
  return count;
}

auto FreeList::largest_block() const -> uword {
  uword largest = 0;
  ForEachBlock([&](uword, const uword size) { largest = std::max(largest, size); });
  return largest;
}
}  // namespace kura::heap
