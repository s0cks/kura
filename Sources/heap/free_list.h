#ifndef KURA_HEAP_FREE_LIST_H
#define KURA_HEAP_FREE_LIST_H

#include "heap/base.h"
#include "heap/header.h"
#include "heap/page_provider.h"

namespace kura::heap {
// The free blocks of the tenured space, kept in address order inside the free memory itself.
//
// Allocation takes the smallest block that fits (best fit) and splits off the remainder when it is big enough to be a
// block of its own. Freeing puts a block back in address order and merges it with the free blocks next to it, so the
// space does not fragment itself into unusable crumbs while the sweeper hands blocks back.
class FreeList {
 private:
  struct FreeBlock {
    ObjectHeader header;  // kind kFree, size = the whole block
    FreeBlock* next;
  };
  static_assert(sizeof(FreeBlock) == kMinBlockSize);

  Region region_{};
  FreeBlock* head_ = nullptr;
  uword free_bytes_ = 0;

 public:
  FreeList() = default;

  // Makes the whole region one free block.
  void Init(const Region& region);

  // Takes a block of at least `size` bytes. `*actual` is the size of the block returned, which is larger than `size`
  // when the leftover was too small to keep as a block. Returns kUnallocated if no block is big enough.
  auto TryAllocate(uword size, uword* actual) -> uword;

  // Returns [address, address + size) to the list.
  void Free(uword address, uword size);

  inline auto free_bytes() const -> uword {
    return free_bytes_;
  }

  auto num_blocks() const -> uword;
  auto largest_block() const -> uword;

  template <typename F>
  void ForEachBlock(F&& visit) const {
    for (const auto* block = head_; block != nullptr; block = block->next)
      visit(reinterpret_cast<uword>(block), block->header.size());
  }
};
}  // namespace kura::heap

#endif  // KURA_HEAP_FREE_LIST_H
