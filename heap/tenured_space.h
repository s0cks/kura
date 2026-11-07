#ifndef KURA_HEAP_TENURED_SPACE_H
#define KURA_HEAP_TENURED_SPACE_H

#include "heap/base.h"
#include "heap/free_list.h"
#include "heap/header.h"
#include "heap/page_provider.h"

namespace kura::heap {
// Large objects and objects that outlived the model space. Nothing in here ever moves.
//
// The whole span is reserved up front but only touched as it is allocated, so a generous reservation costs address
// space, not memory. Objects are carved from a FreeList; the sweeper will return dead ones with Free().
class TenuredSpace {
 private:
  PageProvider* provider_;
  Region region_{};
  FreeList free_list_{};
  uword high_water_ = 0;
  uword allocations_ = 0;

 public:
  TenuredSpace(PageProvider* provider, uword size);
  ~TenuredSpace();
  TenuredSpace(const TenuredSpace&) = delete;
  TenuredSpace(TenuredSpace&&) = delete;
  auto operator=(const TenuredSpace&) -> TenuredSpace& = delete;
  auto operator=(TenuredSpace&&) -> TenuredSpace& = delete;

  // `*actual` is the size of the block, which an object records as its size so a walk of the space stays exact.
  auto TryAllocate(uword size, uword* actual) -> uword;
  void Free(uword address, uword size);

  inline auto Contains(const uword address) const -> bool {
    return region_.Contains(address);
  }

  inline auto reserved() const -> uword {
    return region_.size();
  }

  inline auto used() const -> uword {
    return region_.size() - free_list_.free_bytes();
  }

  inline auto high_water() const -> uword {
    return high_water_ > used() ? high_water_ : used();
  }

  inline auto allocations() const -> uword {
    return allocations_;
  }

  inline auto free_list() const -> const FreeList& {
    return free_list_;
  }

  // Walks every object in address order; free blocks are skipped.
  template <typename F>
  void ForEachObject(F&& visit) const {
    uword address = region_.start();
    const auto end = region_.start() + RoundDown(region_.size(), kObjectAlignment);
    while (address < end) {
      const auto header = reinterpret_cast<const ObjectHeader*>(address);
      KURA_HEAP_CHECK(header->size() >= kMinBlockSize, "(corrupt header at %p)", reinterpret_cast<void*>(address));
      if (!header->IsFree())
        visit(address);
      address += header->size();
    }
  }
};
}  // namespace kura::heap

#endif  // KURA_HEAP_TENURED_SPACE_H
