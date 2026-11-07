#ifndef KURA_HEAP_VALUE_H
#define KURA_HEAP_VALUE_H

#include <cstdint>

#include "heap/base.h"

namespace kura::heap {
// A tagged value, laid out exactly like kura::rt::KuraValue (backend/runtime_abi.h); abi_check.cc keeps the two in
// step. The heap keeps its own copy so it doesn't depend on the compiler's headers.
struct Value {
  uint64_t tag;
  uint64_t bits;
};
static_assert(sizeof(Value) == 16);

namespace tag {
static constexpr uint64_t kInvalid = 0;
static constexpr uint64_t kNone = 1;
static constexpr uint64_t kBool = 2;
static constexpr uint64_t kInt = 3;
static constexpr uint64_t kFloat = 4;
static constexpr uint64_t kString = 5;
static constexpr uint64_t kColor = 6;
static constexpr uint64_t kPx = 7;
static constexpr uint64_t kPct = 8;
static constexpr uint64_t kFr = 9;
static constexpr uint64_t kNode = 10;
static constexpr uint64_t kSeq = 11;
static constexpr uint64_t kRecord = 12;
static constexpr uint64_t kFunction = 13;
static constexpr uint64_t kHandle = 14;
static constexpr uint64_t kVoid = 15;
static constexpr uint64_t kVariantBit = 1ULL << 63;
}  // namespace tag

constexpr auto IsVariantTag(const uint64_t t) -> bool {
  return (t & tag::kVariantBit) != 0;
}

// Whether a value with this tag may hold the address of a heap object in its `bits`. The collector treats only the
// addresses that fall inside one of the heap's spaces as objects, so a "may" is enough:
//
//   - String and Color are string objects; Seq, Record and Node are the matching objects.
//   - A variant's payload is a boxed value (null for a bare variant).
//   - A Function is a closure object, or the address of compiled code for a function that captures nothing: the code
//     address is not in the heap, so it is skipped by the range check.
//   - A Handle is an opaque host pointer, never a heap object.
constexpr auto TagMayHoldHeapPointer(const uint64_t t) -> bool {
  switch (t) {
    case tag::kString:
    case tag::kColor:
    case tag::kNode:
    case tag::kSeq:
    case tag::kRecord:
    case tag::kFunction:
      return true;
    default:
      return IsVariantTag(t);
  }
}
}  // namespace kura::heap

#endif  // KURA_HEAP_VALUE_H
