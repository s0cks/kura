#ifndef KURA_PLATFORM_H
#define KURA_PLATFORM_H

#include <cstdint>
#include <print>
#include <source_location>
#include <stdarg.h>

#if defined(KURA_DEBUG) || defined(KURA_HEAP_DEBUG)
#define KURA_HEAP_DEBUG_ENABLED 1
#endif  // KURA_DEBUG || KURA_HEAP_DEBUG

#ifdef KURA_HEAP_DEBUG_ENABLED
#define KURA_HEAP_DCHECK(cond, ...) KURA_HEAP_CHECK(cond, __VA_ARGS__)
#else
#define KURA_HEAP_DCHECK(cond, ...)
#endif  // KURA_HEAP_DEBUG_ENABLED

namespace kura {
using word = intptr_t;
using uword = uintptr_t;

static_assert(sizeof(uword) == 8, "the kura heap requires a 64-bit target: a Value's payload is a 64-bit word");

static constexpr uword kWordSize = sizeof(uword);
static constexpr uword kKiB = 1024;
static constexpr uword kMiB = 1024 * kKiB;

// Every object starts on an 8-byte boundary: a Value is two 64-bit words and nothing in the language needs more.
static constexpr uword kObjectAlignment = 8;

// The smallest block the heap hands out or tracks. An object is a header and at least one payload word (a moved
// object's forwarding address lives there), and a free block is a header and a `next` pointer: both are 16 bytes.
static constexpr uword kMinBlockSize = 16;

static constexpr uword kUnallocated = 0;

constexpr auto IsPow2(const uword value) -> bool {
  return value != 0 && (value & (value - 1)) == 0;
}

constexpr auto RoundUp(const uword value, const uword alignment) -> uword {
  return (value + (alignment - 1)) & ~(alignment - 1);
}

constexpr auto RoundDown(const uword value, const uword alignment) -> uword {
  return value & ~(alignment - 1);
}

constexpr auto IsAligned(const uword value, const uword alignment) -> bool {
  return (value & (alignment - 1)) == 0;
}

namespace heap {
static inline void Fatal(std::string s, const std::source_location loc = std::source_location::current()) {
  std::fflush(stdout);
  std::fprintf(stderr, "%s:%c", loc.file_name(), loc.line());
  std::fprintf(stderr, "%s\n", s.c_str());
  std::fflush(stderr);
  std::abort();
}

#define KURA_HEAP_CHECK(cond, ...)                                              \
  do {                                                                          \
    if (!(cond))                                                                \
      ::kura::heap::Fatal(std::format("check failed: " #cond " " __VA_ARGS__)); \
  } while (false)
}  // namespace heap
}  // namespace kura

#endif  // KURA_PLATFORM_H
