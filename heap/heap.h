#ifndef KURA_HEAP_HEAP_H
#define KURA_HEAP_HEAP_H

#include <iosfwd>
#include <vector>

#include "heap/arena.h"
#include "heap/base.h"
#include "heap/header.h"
#include "heap/objects.h"
#include "heap/page_provider.h"
#include "heap/semispace.h"
#include "heap/tenured_space.h"

namespace kura::heap {
struct HeapOptions {
  // Both halves of the model space together. The model (everything reachable from the current Model value) has to fit
  // in half of this.
  uword model_space_size = 8 * kMiB;

  // Address space reserved for the tenured space. It costs memory only as it is used.
  uword tenured_size = 64 * kMiB;

  // An object at least this big is allocated straight into the tenured space: copying it on every flip would cost more
  // than the pointer bump saved.
  uword large_object_threshold = 4 * kKiB;

  // `view`'s temporaries. Chunked and growing; `max_size` 0 means no cap.
  Arena::Options frame = {.chunk_size = 256 * kKiB, .max_size = 0};

  // A Message and `update`'s temporaries.
  Arena::Options tick = {.chunk_size = 64 * kKiB, .max_size = 0};

  // Memory that is never freed: the runtime's own tables (the layout cache, handler tables) and C strings handed to
  // code that keeps them.
  Arena::Options permanent = {.chunk_size = 64 * kKiB, .max_size = 0};

  // Where memory comes from. Null means anonymous pages from the OS.
  PageProvider* provider = nullptr;
};

struct SpaceStats {
  uword reserved = 0;
  uword used = 0;
  uword high_water = 0;
  uword allocations = 0;
};

struct HeapStats {
  SpaceStats frame{};
  SpaceStats tick{};
  SpaceStats model{};
  SpaceStats tenured{};
  SpaceStats permanent{};

  // Allocations that wanted the model space but found it full and went to the tenured space instead (see
  // Heap::AllocateModel).
  uword model_overflow_allocations = 0;
  uword model_overflow_bytes = 0;
};

// The memory manager for a running Kura program.
//
// The compiler picks a lifetime for every allocation and the matching entry point is called:
//
//   stack      escape analysis: never reaches the heap.
//   frame      escapes its function but not the frame being drawn: `view`.
//   tick       escapes its function but not the update that made it: a Message, `update`'s temporaries.
//   model      reachable from the Model that `init`/`update` returns.
//   tenured    large objects, and (later) objects that outlive enough flips.
//
// Collection is not implemented yet, but the heap is built so that it needs no stack maps: it only ever happens
// between ticks, when the frame and tick spaces are empty and nothing is running. The root set at that point is the
// Model value the host holds, plus the tenured objects in remembered(). See NeedsCollection().
class Heap {
 public:
  // Called when an allocation cannot be satisfied. The default prints the heap's state and aborts. A handler that
  // returns makes the allocation fail (the caller gets a null object): meant for tests and for hosts that would
  // rather unwind than abort.
  using OutOfMemoryHandler = void (*)(const Heap& heap, Space space, uword requested);

 private:
  HeapOptions options_;
  PageProvider* provider_;
  Arena frame_;
  Arena tick_;
  Arena permanent_;
  ModelSpace model_;
  TenuredSpace tenured_;
  OutOfMemoryHandler oom_handler_;
  std::vector<uword> remembered_{};
  uword model_overflow_allocations_ = 0;
  uword model_overflow_bytes_ = 0;

  auto AllocateRaw(Space space, uword total, Space* actual_space, uword* actual_size) -> uword;

 public:
  explicit Heap(const HeapOptions& options = {});
  ~Heap();
  Heap(const Heap&) = delete;
  Heap(Heap&&) = delete;
  auto operator=(const Heap&) -> Heap& = delete;
  auto operator=(Heap&&) -> Heap& = delete;

  inline auto options() const -> const HeapOptions& {
    return options_;
  }

  //--- allocation ----------------------------------------------------------------------------------------------
  // Allocates an object of `kind` with `payload` bytes after its header in `space` (kFrame, kTick, kModel or
  // kTenured). The header is written and the payload zeroed. Returns null only if an out-of-memory handler returned.
  auto AllocateObject(Space space, ObjKind kind, uword payload) -> HeapObject*;

  // The model space is a bump allocator and runs out mid-tick if a program builds a large model. Collection can't run
  // then, so the allocation overflows into the tenured space instead and is counted: a nonzero overflow count is the
  // signal to collect at the next safe point. The returned object's header says which space it really landed in.
  inline auto AllocateModel(const ObjKind kind, const uword payload) -> HeapObject* {
    return AllocateObject(Space::kModel, kind, payload);
  }

  // The default policy for an object that outlives its function and whose lifetime isn't known any better: large
  // objects go to the tenured space, the rest to the model space.
  auto Allocate(ObjKind kind, uword payload) -> HeapObject*;

  // Raw memory that is never freed or moved, aligned to `alignment` (a power of two, at most the page size).
  auto AllocatePermanent(uword size, uword alignment = kObjectAlignment) -> void*;

  //--- lifetimes -----------------------------------------------------------------------------------------------
  // The frame is over: everything allocated in the frame space is gone.
  void ResetFrame();

  // The update is over: everything allocated in the tick space is gone.
  void ResetTick();

  // Whether the model space has filled up (or spilled over) enough that the next safe point should collect. Looks at
  // `threshold_percent` of one half of the model space.
  auto NeedsCollection(unsigned threshold_percent = 75) const -> bool;

  //--- collector hooks ---------------------------------------------------------------------------------------
  inline auto model() -> ModelSpace& {
    return model_;
  }

  inline auto model() const -> const ModelSpace& {
    return model_;
  }

  inline auto tenured() -> TenuredSpace& {
    return tenured_;
  }

  inline auto tenured() const -> const TenuredSpace& {
    return tenured_;
  }

  inline auto frame() const -> const Arena& {
    return frame_;
  }

  inline auto tick() const -> const Arena& {
    return tick_;
  }

  // Tenured objects built since the last collection that may point into the model space. A tenured object is
  // immutable once built, so the only time it can point at a younger object is while it is being filled in right
  // after allocation; recording every pointerful tenured allocation therefore stands in for a write barrier. The
  // collector scans these as extra roots and clears the list.
  inline auto remembered() const -> const std::vector<uword>& {
    return remembered_;
  }

  inline void ClearRemembered() {
    remembered_.clear();
  }

  //--- queries ---------------------------------------------------------------------------------------------
  // Which space an address is in. kStatic means it is not an address the heap manages (a literal in the compiled
  // module, a code address, a host pointer, null).
  auto SpaceOf(uword address) const -> Space;

  inline auto Contains(const uword address) const -> bool {
    return SpaceOf(address) != Space::kStatic;
  }

  auto Stats() const -> HeapStats;

  // Calls `visit(HeapObject*)` for every object in `space`. Not for kStatic or the permanent region.
  template <typename F>
  void ForEachObject(const Space space, F&& visit) {
    const auto call = [&](const uword address) {
      visit(reinterpret_cast<HeapObject*>(address));
    };
    switch (space) {
      case Space::kFrame:
        frame_.ForEachObject(call);
        break;
      case Space::kTick:
        tick_.ForEachObject(call);
        break;
      case Space::kModel:
        model_.ForEachObject(call);
        break;
      case Space::kTenured:
        tenured_.ForEachObject(call);
        break;
      case Space::kStatic:
        break;
    }
  }

  // Checks the heap's invariants: every header in every space is well formed and in the right space, and no object in
  // the model or tenured space points into the frame or tick space (the escape analysis's promise). Returns whether
  // all hold; each violation is described on `out` if it is given.
  auto Verify(std::ostream* out = nullptr) -> bool;

  void SetOutOfMemoryHandler(OutOfMemoryHandler handler);
  void Dump(std::ostream& out) const;

  //--- the current heap --------------------------------------------------------------------------------------
  // The runtime functions compiled code calls (kura_rt_*) take no heap argument; they use the current thread's heap.
  static auto Current() -> Heap*;
  static void SetCurrent(Heap* heap);
};
}  // namespace kura::heap

#endif  // KURA_HEAP_HEAP_H
