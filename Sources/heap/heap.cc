#include "heap/heap.h"

#include <cstring>
#include <ostream>

namespace kura::heap {
namespace {
thread_local Heap* current_heap = nullptr;

void DefaultOutOfMemory(const Heap& heap, const Space space, const uword requested) {
  using ull = unsigned long long;
  Fatal(__FILE__, __LINE__,
        "out of memory: cannot allocate %llu bytes in the %s space (model %llu/%llu, tenured %llu/%llu)",
        static_cast<ull>(requested), SpaceName(space), static_cast<ull>(heap.model().used()),
        static_cast<ull>(heap.model().capacity()), static_cast<ull>(heap.tenured().used()),
        static_cast<ull>(heap.tenured().reserved()));
}
}  // namespace

Heap::Heap(const HeapOptions& options) :
  options_(options),
  provider_(options.provider ? options.provider : PageProvider::Default()),
  frame_(provider_, options.frame),
  tick_(provider_, options.tick),
  permanent_(provider_, options.permanent),
  model_(provider_, options.model_space_size),
  tenured_(provider_, options.tenured_size),
  oom_handler_(&DefaultOutOfMemory) {}

Heap::~Heap() {
  if (current_heap == this)
    current_heap = nullptr;
}

auto Heap::Current() -> Heap* {
  return current_heap;
}

void Heap::SetCurrent(Heap* heap) {
  current_heap = heap;
}

void Heap::SetOutOfMemoryHandler(const OutOfMemoryHandler handler) {
  oom_handler_ = handler ? handler : &DefaultOutOfMemory;
}

auto Heap::AllocateRaw(const Space space, const uword total, Space* actual_space, uword* actual_size) -> uword {
  *actual_space = space;
  *actual_size = total;
  switch (space) {
    case Space::kFrame:
      return frame_.TryAllocate(total);
    case Space::kTick:
      return tick_.TryAllocate(total);
    case Space::kModel: {
      const auto address = model_.TryAllocate(total);
      if (address != kUnallocated)
        return address;
      // Full mid-tick: spill into the tenured space (see AllocateModel).
      uword block = 0;
      const auto spilled = tenured_.TryAllocate(total, &block);
      if (spilled != kUnallocated) {
        *actual_space = Space::kTenured;
        *actual_size = block;
        model_overflow_allocations_++;
        model_overflow_bytes_ += block;
      }
      return spilled;
    }
    case Space::kTenured: {
      uword block = 0;
      const auto address = tenured_.TryAllocate(total, &block);
      *actual_size = block;
      return address;
    }
    case Space::kStatic:
      break;
  }
  KURA_HEAP_CHECK(false, "(objects cannot be allocated in the %s space)", SpaceName(space));
  return kUnallocated;
}

auto Heap::AllocateObject(const Space space, const ObjKind kind, const uword payload) -> HeapObject* {
  const auto total = ObjectSizeFor(payload);
  KURA_HEAP_CHECK(total <= ObjectHeader::kMaxSize, "(an object of %llu bytes is too large)",
                  static_cast<unsigned long long>(total));

  Space actual_space = space;
  uword actual_size = total;
  const auto address = AllocateRaw(space, total, &actual_space, &actual_size);
  if (address == kUnallocated) {
    oom_handler_(*this, space, total);
    return nullptr;
  }

  // Zero everything after the header (including any slack in a tenured block) so the object is scannable at once.
  std::memset(reinterpret_cast<void*>(address + kHeaderSize), 0, actual_size - kHeaderSize);
  const auto obj = reinterpret_cast<HeapObject*>(address);
  obj->header = ObjectHeader::Make(kind, actual_space, actual_size);

  if (actual_space == Space::kTenured && KindHasPointers(kind)) {
    obj->header.set_remembered();
    remembered_.push_back(address);
  }
  return obj;
}

auto Heap::Allocate(const ObjKind kind, const uword payload) -> HeapObject* {
  const auto space = ObjectSizeFor(payload) >= options_.large_object_threshold ? Space::kTenured : Space::kModel;
  return AllocateObject(space, kind, payload);
}

auto Heap::AllocatePermanent(const uword size, const uword alignment) -> void* {
  KURA_HEAP_CHECK(IsPow2(alignment) && alignment <= 4 * kKiB);
  // The arena hands out 8-byte aligned blocks; over-allocate to align more strictly.
  const auto padded = alignment > kObjectAlignment ? size + alignment : size;
  const auto address = permanent_.TryAllocate(padded);
  if (address == kUnallocated) {
    oom_handler_(*this, Space::kStatic, padded);
    return nullptr;
  }
  return reinterpret_cast<void*>(RoundUp(address, alignment));
}

void Heap::ResetFrame() {
  frame_.Reset();
}

void Heap::ResetTick() {
  tick_.Reset();
}

auto Heap::NeedsCollection(const unsigned threshold_percent) const -> bool {
  if (model_overflow_allocations_ > 0)
    return true;
  return model_.used() * 100 >= model_.capacity() * threshold_percent;
}

auto Heap::SpaceOf(const uword address) const -> Space {
  if (model_.Contains(address))
    return Space::kModel;
  if (tenured_.Contains(address))
    return Space::kTenured;
  if (frame_.Contains(address))
    return Space::kFrame;
  if (tick_.Contains(address))
    return Space::kTick;
  return Space::kStatic;
}

static auto ToStats(const Arena& arena) -> SpaceStats {
  return {arena.reserved(), arena.used(), arena.high_water(), arena.allocations()};
}

auto Heap::Stats() const -> HeapStats {
  HeapStats stats{};
  stats.frame = ToStats(frame_);
  stats.tick = ToStats(tick_);
  stats.permanent = ToStats(permanent_);
  stats.model = {model_.reserved(), model_.used(), model_.high_water(), model_.allocations()};
  stats.tenured = {tenured_.reserved(), tenured_.used(), tenured_.high_water(), tenured_.allocations()};
  stats.model_overflow_allocations = model_overflow_allocations_;
  stats.model_overflow_bytes = model_overflow_bytes_;
  return stats;
}

auto Heap::Verify(std::ostream* out) -> bool {
  bool ok = true;
  const auto fail = [&](const Space space, const uword address, const char* what) {
    ok = false;
    if (out)
      *out << "heap: " << SpaceName(space) << " object at 0x" << std::hex << address << std::dec << ": " << what
           << "\n";
  };

  for (const auto space : {Space::kFrame, Space::kTick, Space::kModel, Space::kTenured}) {
    ForEachObject(space, [&](HeapObject* obj) {
      const auto address = obj->address();
      const auto kind = obj->kind();
      if (kind == ObjKind::kInvalid || kind > ObjKind::kBox)
        return fail(space, address, "has an invalid kind");
      if (obj->size() < kMinBlockSize || !IsAligned(obj->size(), kObjectAlignment))
        return fail(space, address, "has a malformed size");
      if (obj->header.forwarded())
        return fail(space, address, "is forwarded outside a collection");
      if (obj->space() != space)
        return fail(space, address, "records the wrong space in its header");
      if (SpaceOf(address) != space)
        return fail(space, address, "is not in the space its header says");

      // The escape analysis's promise: nothing that lives past the tick or frame may point into them.
      if (space == Space::kModel || space == Space::kTenured) {
        VisitPointerSlots(obj, [&](const uint64_t* slot) {
          const auto target = SpaceOf(static_cast<uword>(*slot));
          if (target == Space::kFrame || target == Space::kTick)
            fail(space, address,
                 target == Space::kFrame ? "points into the frame space" : "points into the tick space");
        });
      }
    });
  }
  return ok;
}

void Heap::Dump(std::ostream& out) const {
  const auto stats = Stats();
  const auto line = [&](const char* name, const SpaceStats& s) {
    out << "  " << name << ": used " << s.used << " / reserved " << s.reserved << " (high water " << s.high_water
        << ", " << s.allocations << " allocations)\n";
  };
  out << "Heap:\n";
  line("frame    ", stats.frame);
  line("tick     ", stats.tick);
  line("model    ", stats.model);
  line("tenured  ", stats.tenured);
  line("permanent", stats.permanent);
  out << "  model overflow: " << stats.model_overflow_allocations << " allocations, " << stats.model_overflow_bytes
      << " bytes\n";
}
}  // namespace kura::heap
