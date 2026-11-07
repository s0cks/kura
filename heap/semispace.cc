#include "heap/semispace.h"

#include <algorithm>
#include <cstring>

namespace kura::heap {
ModelSpace::ModelSpace(PageProvider* provider, const uword total_size) :
  provider_(provider) {
  KURA_HEAP_CHECK(provider_ != nullptr);
  region_ = provider_->Acquire(total_size);
  KURA_HEAP_CHECK(!region_.empty(), "(cannot reserve %llu bytes for the model space)",
                  static_cast<unsigned long long>(total_size));
  const auto half = RoundDown(region_.size() / 2, kObjectAlignment);
  from_ = {region_.start(), region_.start() + half, region_.start()};
  to_ = {region_.start() + half, region_.start() + 2 * half, region_.start() + half};
}

ModelSpace::~ModelSpace() {
  provider_->Release(region_);
}

void ModelSpace::Poison(const Half& half) {
#ifdef KURA_HEAP_DEBUG_ENABLED
  std::memset(reinterpret_cast<void*>(half.start), 0xDD, half.cursor - half.start);
#else
  (void)half;
#endif  // KURA_HEAP_DEBUG_ENABLED
}

auto ModelSpace::TryAllocate(uword size) -> uword {
  size = RoundUp(size, kObjectAlignment);
  if (from_.end - from_.cursor < size)
    return kUnallocated;
  const auto address = from_.cursor;
  from_.cursor += size;
  allocations_++;
  high_water_ = std::max(high_water_, used());
  return address;
}

auto ModelSpace::TryAllocateInToSpace(uword size) -> uword {
  size = RoundUp(size, kObjectAlignment);
  if (to_.end - to_.cursor < size)
    return kUnallocated;
  const auto address = to_.cursor;
  to_.cursor += size;
  return address;
}

void ModelSpace::Flip() {
  std::swap(from_, to_);
  Poison(to_);
  to_.cursor = to_.start;
  flips_++;
}

void ModelSpace::Reset() {
  Poison(from_);
  Poison(to_);
  from_.cursor = from_.start;
  to_.cursor = to_.start;
}
}  // namespace kura::heap
