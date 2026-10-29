#ifndef KURA_HEAP_SEMISPACE_H
#define KURA_HEAP_SEMISPACE_H

#include "heap/base.h"
#include "heap/header.h"
#include "heap/page_provider.h"

namespace kura::heap {
// The model space: two equal halves, one allocated into at a time.
//
// Allocation is a pointer bump in the from-space. Nothing here decides when a collection happens or what survives it:
// when the collector copies the live model it allocates the copies with TryAllocateInToSpace, then calls Flip(), which
// makes the to-space the new from-space and empties the old one. Between flips this is just a bump allocator.
class ModelSpace {
 private:
  struct Half {
    uword start = 0;
    uword end = 0;
    uword cursor = 0;
  };

  PageProvider* provider_;
  Region region_{};
  Half from_{};
  Half to_{};
  uword high_water_ = 0;
  uword allocations_ = 0;
  uword flips_ = 0;

  static void Poison(const Half& half);

 public:
  // `total_size` is both halves together.
  ModelSpace(PageProvider* provider, uword total_size);
  ~ModelSpace();
  ModelSpace(const ModelSpace&) = delete;
  ModelSpace(ModelSpace&&) = delete;
  auto operator=(const ModelSpace&) -> ModelSpace& = delete;
  auto operator=(ModelSpace&&) -> ModelSpace& = delete;

  auto TryAllocate(uword size) -> uword;
  auto TryAllocateInToSpace(uword size) -> uword;
  void Flip();

  // Empties both halves.
  void Reset();

  inline auto Contains(const uword address) const -> bool {
    return region_.Contains(address);
  }

  inline auto ContainsInFromSpace(const uword address) const -> bool {
    return address >= from_.start && address < from_.cursor;
  }

  inline auto ContainsInToSpace(const uword address) const -> bool {
    return address >= to_.start && address < to_.cursor;
  }

  // The size of one half.
  inline auto capacity() const -> uword {
    return from_.end - from_.start;
  }

  inline auto used() const -> uword {
    return from_.cursor - from_.start;
  }

  inline auto remaining() const -> uword {
    return from_.end - from_.cursor;
  }

  inline auto reserved() const -> uword {
    return region_.size();
  }

  inline auto high_water() const -> uword {
    return high_water_ > used() ? high_water_ : used();
  }

  inline auto allocations() const -> uword {
    return allocations_;
  }

  inline auto flips() const -> uword {
    return flips_;
  }

  inline auto from_space() const -> Region {
    return {from_.start, from_.end - from_.start};
  }

  inline auto to_space() const -> Region {
    return {to_.start, to_.end - to_.start};
  }

  template <typename F>
  void ForEachObject(F&& visit) const {
    uword address = from_.start;
    while (address < from_.cursor) {
      const auto header = reinterpret_cast<const ObjectHeader*>(address);
      KURA_HEAP_CHECK(header->size() >= kMinBlockSize, "(corrupt header at %p)", reinterpret_cast<void*>(address));
      visit(address);
      address += header->size();
    }
  }
};
}  // namespace kura::heap

#endif  // KURA_HEAP_SEMISPACE_H
