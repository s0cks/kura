#ifndef KURA_HEAP_ARENA_H
#define KURA_HEAP_ARENA_H

#include <vector>

#include "heap/base.h"
#include "heap/header.h"
#include "heap/page_provider.h"

namespace kura::heap {
// A bump allocator that is emptied in one step. Backs the frame and tick scratch spaces (and the permanent region).
//
// Memory comes in chunks. An arena that outgrows its first chunk takes another one rather than failing or moving
// anything, and the next Reset() folds everything into a single chunk sized to what the cycle needed: after a frame or
// two the arena is one contiguous span, which is what lets generated code bump-allocate inline.
//
// `max_size` caps the total memory the arena may hold (0 means no cap). A game wants a hard, deterministic budget:
// going over it makes TryAllocate fail instead of asking the OS for more.
class Arena {
 public:
  struct Options {
    uword chunk_size = 256 * kKiB;
    uword max_size = 0;
  };

 private:
  struct Chunk {
    Region region;
    uword cursor;
  };

  PageProvider* provider_;
  Options options_;
  std::vector<Chunk> chunks_{};
  size_t current_ = 0;
  uword reserved_ = 0;
  uword used_ = 0;
  uword high_water_ = 0;
  uword allocations_ = 0;
  uword resets_ = 0;

 public:
  Arena(PageProvider* provider, const Options& options);
  ~Arena();
  Arena(const Arena&) = delete;
  Arena(Arena&&) = delete;
  auto operator=(const Arena&) -> Arena& = delete;
  auto operator=(Arena&&) -> Arena& = delete;

  // Returns the address of `size` bytes (rounded up to the object alignment), or kUnallocated if the cap is reached or
  // the provider is out of memory. The memory is not zeroed.
  auto TryAllocate(uword size) -> uword;

  // Forgets everything allocated since the last reset. In a debug build the memory is poisoned so a stale pointer is
  // caught instead of silently reading the next frame's data.
  void Reset();

  auto Contains(uword address) const -> bool;

  // Bytes handed out since the last reset.
  inline auto used() const -> uword {
    return used_;
  }

  // The most `used()` has been at any point, across resets.
  inline auto high_water() const -> uword {
    return high_water_ > used_ ? high_water_ : used_;
  }

  inline auto reserved() const -> uword {
    return reserved_;
  }

  inline auto allocations() const -> uword {
    return allocations_;
  }

  inline auto resets() const -> uword {
    return resets_;
  }

  inline auto num_chunks() const -> size_t {
    return chunks_.size();
  }

  // Walks the objects in the arena, in allocation order. Only for arenas that hold nothing but objects (every
  // allocation starts with an ObjectHeader): not the permanent region.
  template <typename F>
  void ForEachObject(F&& visit) const {
    for (const auto& chunk : chunks_) {
      uword address = chunk.region.start();
      while (address < chunk.cursor) {
        const auto header = reinterpret_cast<const ObjectHeader*>(address);
        KURA_HEAP_CHECK(header->size() >= kMinBlockSize, "(corrupt header at %p)", reinterpret_cast<void*>(address));
        visit(address);
        address += header->size();
      }
    }
  }
};
}  // namespace kura::heap

#endif  // KURA_HEAP_ARENA_H
