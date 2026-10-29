#include "heap/arena.h"

#include <algorithm>
#include <cstring>

namespace kura::heap {
static constexpr unsigned char kPoison = 0xDD;

Arena::Arena(PageProvider* provider, const Options& options) :
  provider_(provider),
  options_(options) {
  KURA_HEAP_CHECK(provider_ != nullptr);
  KURA_HEAP_CHECK(options_.chunk_size > 0);
}

Arena::~Arena() {
  for (const auto& chunk : chunks_)
    provider_->Release(chunk.region);
}

auto Arena::TryAllocate(uword size) -> uword {
  size = RoundUp(size, kObjectAlignment);
  KURA_HEAP_DCHECK(size > 0);
  while (true) {
    if (current_ < chunks_.size()) {
      auto& chunk = chunks_[current_];
      if (chunk.region.end() - chunk.cursor >= size) {
        const auto address = chunk.cursor;
        chunk.cursor += size;
        used_ += size;
        allocations_++;
        high_water_ = std::max(high_water_, used_);
        return address;
      }
      current_++;
      continue;
    }
    // Out of chunks: take another, unless that would break the cap.
    auto want = std::max(options_.chunk_size, RoundUp(size, kKiB));
    if (options_.max_size != 0) {
      const auto room = options_.max_size > reserved_ ? options_.max_size - reserved_ : 0;
      if (size > room)
        return kUnallocated;
      want = std::min(want, room);
    }
    const auto request = want;
    const auto region = provider_->Acquire(request);
    if (region.empty())
      return kUnallocated;
    reserved_ += region.size();
    chunks_.push_back({region, region.start()});
    current_ = chunks_.size() - 1;
  }
}

void Arena::Reset() {
  high_water_ = std::max(high_water_, used_);
  resets_++;

  if (chunks_.size() > 1) {
    // The last cycle spilled into several chunks: replace them with one that would have held it all (plus a quarter for
    // the tails left unused at the end of each chunk), so the next cycle runs in a single span.
    auto want = std::max(options_.chunk_size, used_ + used_ / 4);
    if (options_.max_size != 0)
      want = std::min(want, options_.max_size);
    const auto region = provider_->Acquire(want);
    if (!region.empty()) {
      for (const auto& chunk : chunks_)
        provider_->Release(chunk.region);
      chunks_.clear();
      chunks_.push_back({region, region.start()});
      reserved_ = region.size();
    }
  }

  for (auto& chunk : chunks_) {
#ifdef KURA_HEAP_DEBUG_ENABLED
    std::memset(reinterpret_cast<void*>(chunk.region.start()), kPoison, chunk.cursor - chunk.region.start());
#endif  // KURA_HEAP_DEBUG_ENABLED
    chunk.cursor = chunk.region.start();
  }
  current_ = 0;
  used_ = 0;
}

auto Arena::Contains(const uword address) const -> bool {
  return std::any_of(chunks_.begin(), chunks_.end(), [&](const Chunk& chunk) {
    return address >= chunk.region.start() && address < chunk.cursor;
  });
}
}  // namespace kura::heap
