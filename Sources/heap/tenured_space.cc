#include "heap/tenured_space.h"

#include <algorithm>

namespace kura::heap {
TenuredSpace::TenuredSpace(PageProvider* provider, const uword size) :
  provider_(provider) {
  KURA_HEAP_CHECK(provider_ != nullptr);
  region_ = provider_->Acquire(size);
  KURA_HEAP_CHECK(!region_.empty(), "(cannot reserve %llu bytes for the tenured space)",
                  static_cast<unsigned long long>(size));
  free_list_.Init(region_);
}

TenuredSpace::~TenuredSpace() {
  provider_->Release(region_);
}

auto TenuredSpace::TryAllocate(const uword size, uword* actual) -> uword {
  const auto address = free_list_.TryAllocate(size, actual);
  if (address == kUnallocated)
    return kUnallocated;
  allocations_++;
  high_water_ = std::max(high_water_, used());
  return address;
}

void TenuredSpace::Free(const uword address, const uword size) {
  free_list_.Free(address, size);
}
}  // namespace kura::heap
