#include "heap/page_provider.h"

#include <sys/mman.h>
#include <unistd.h>

namespace kura::heap {
namespace {
class MmapPageProvider : public PageProvider {
 public:
  auto Acquire(const uword size) -> Region override {
    if (size == 0)
      return {};
    const auto page = static_cast<uword>(sysconf(_SC_PAGESIZE));
    const auto total = RoundUp(size, page);
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_NORESERVE
    // Large reservations (the tenured space) cost address space, not memory, until they are touched.
    flags |= MAP_NORESERVE;
#endif  // MAP_NORESERVE
    void* ptr = mmap(nullptr, total, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (ptr == MAP_FAILED)
      return {};
    return {reinterpret_cast<uword>(ptr), total};
  }

  void Release(const Region& region) override {
    if (!region.empty())
      munmap(reinterpret_cast<void*>(region.start()), region.size());
  }
};
}  // namespace

auto PageProvider::Default() -> PageProvider* {
  static MmapPageProvider instance{};
  return &instance;
}
}  // namespace kura::heap
