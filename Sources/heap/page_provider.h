#ifndef KURA_HEAP_PAGE_PROVIDER_H
#define KURA_HEAP_PAGE_PROVIDER_H

#include "heap/base.h"

namespace kura::heap {
// A span of address space: [start, start + size).
class Region {
 private:
  uword start_{0};
  uword size_{0};

 public:
  constexpr Region() = default;
  constexpr Region(const uword start, const uword size) :
    start_(start),
    size_(size) {}

  constexpr auto start() const -> uword {
    return start_;
  }

  constexpr auto size() const -> uword {
    return size_;
  }

  constexpr auto end() const -> uword {
    return start_ + size_;
  }

  constexpr auto empty() const -> bool {
    return size_ == 0;
  }

  constexpr auto Contains(const uword address) const -> bool {
    return address >= start_ && address < start_ + size_;
  }

  inline auto Contains(const void* ptr) const -> bool {
    return Contains(reinterpret_cast<uword>(ptr));
  }
};

// Where the heap's memory comes from. A host embedding Kura (a game engine, say) can supply its own provider so
// Kura's memory is carved out of the host's budget; the default maps anonymous pages.
class PageProvider {
 public:
  virtual ~PageProvider() = default;

  // Returns at least `size` bytes of page-aligned, readable and writable memory, or an empty Region. The contents are
  // unspecified: nothing in the heap relies on fresh memory being zero.
  virtual auto Acquire(uword size) -> Region = 0;
  virtual void Release(const Region& region) = 0;

  static auto Default() -> PageProvider*;
};
}  // namespace kura::heap

#endif  // KURA_HEAP_PAGE_PROVIDER_H
