// The runtime side of `kurac --trace`: the two calls a compiled zone makes (see TracyBeginZoneInstr), and the
// bookkeeping that keeps a thread's zones balanced when a test panics out of the middle of one.
//
// Compiled code only passes a pointer to its constant source location. The runtime keeps the open zones, so the
// compiled code needs neither a context value to carry from the begin to the end nor an end on every way out.
#include <cstdint>
#include <vector>

#include "backend/runtime_abi.h"

#if defined(KURA_TRACING_ENABLED) && defined(TRACY_ENABLE)
#include <tracy/TracyC.h>
#define KURA_HAS_TRACY_C 1
#endif

namespace {
#ifdef KURA_HAS_TRACY_C
// The zones that are open on this thread, innermost last.
thread_local std::vector<TracyCZoneCtx> tl_zones{};
#endif
}  // namespace

extern "C" {
void kura_rt_tracy_zone_begin(const void* srcloc) {
#ifdef KURA_HAS_TRACY_C
  tl_zones.push_back(___tracy_emit_zone_begin(static_cast<const ___tracy_source_location_data*>(srcloc), 1));
#else
  (void)srcloc;
#endif
}

void kura_rt_tracy_zone_end() {
#ifdef KURA_HAS_TRACY_C
  if (tl_zones.empty())
    return;
  ___tracy_emit_zone_end(tl_zones.back());
  tl_zones.pop_back();
#endif
}

// A zone with a name that is only known now (a test's), which Tracy wants copied, because there is no constant to
// point to. It is closed with `kura_rt_tracy_zone_end`, like the others.
void kura_rt_tracy_zone_begin_named(const char* name, const uint64_t length, const uint32_t color) {
#ifdef KURA_HAS_TRACY_C
  const auto srcloc = ___tracy_alloc_srcloc_name(0, "", 0, "", 0, name, static_cast<size_t>(length), color);
  tl_zones.push_back(___tracy_emit_zone_begin_alloc(srcloc, 1));
#else
  (void)name;
  (void)length;
  (void)color;
#endif
}

uint64_t kura_rt_tracy_depth() {
#ifdef KURA_HAS_TRACY_C
  return tl_zones.size();
#else
  return 0;
#endif
}

void kura_rt_tracy_unwind(const uint64_t depth) {
#ifdef KURA_HAS_TRACY_C
  // a panic jumps out of the functions it was in, past their ends: close what they left open
  while (tl_zones.size() > depth)
    kura_rt_tracy_zone_end();
#else
  (void)depth;
#endif
}

auto kura_rt_tracy_enabled() -> int {
#ifdef KURA_HAS_TRACY_C
  return 1;
#else
  return 0;
#endif
}
}
