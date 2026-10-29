#ifndef KURA_HEAP_BASE_H
#define KURA_HEAP_BASE_H

#include <cstdint>

// The heap is deliberately free of the compiler's dependencies (LLVM, xxhash, ANTLR): it is what a small host
// runtime links, so it only needs the standard library.

#if defined(KURA_DEBUG) || defined(KURA_HEAP_DEBUG)
#define KURA_HEAP_DEBUG_ENABLED 1
#endif  // KURA_DEBUG || KURA_HEAP_DEBUG

namespace kura::heap {
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

[[noreturn]] void Fatal(const char* file, int line, const char* format, ...) __attribute__((format(printf, 3, 4)));
}  // namespace kura::heap

#define KURA_HEAP_CHECK(cond, ...)                                                           \
  do {                                                                                       \
    if (!(cond))                                                                             \
      ::kura::heap::Fatal(__FILE__, __LINE__, "check failed: " #cond " " __VA_ARGS__);       \
  } while (false)

#ifdef KURA_HEAP_DEBUG_ENABLED
#define KURA_HEAP_DCHECK(cond, ...) KURA_HEAP_CHECK(cond, __VA_ARGS__)
#else
#define KURA_HEAP_DCHECK(cond, ...) ((void)sizeof(!(cond)))
#endif  // KURA_HEAP_DEBUG_ENABLED

#endif  // KURA_HEAP_BASE_H
