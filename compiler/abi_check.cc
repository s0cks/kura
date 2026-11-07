// Keeps the heap's copy of the value ABI in step with the runtime's. This only includes declarations, so it needs
// nothing beyond the standard library, but it fails the build the moment either side changes a tag or the layout of a
// KuraValue.
#include <cstddef>

#include "backend/runtime_abi.h"
#include "heap/value.h"

namespace kura::heap {
namespace {
using kura::rt::KuraValue;

static_assert(sizeof(Value) == sizeof(KuraValue));
static_assert(alignof(Value) == alignof(KuraValue));
static_assert(offsetof(Value, tag) == offsetof(KuraValue, tag));
static_assert(offsetof(Value, bits) == offsetof(KuraValue, bits));

static_assert(tag::kInvalid == rt::kTagInvalid);
static_assert(tag::kNone == rt::kTagNone);
static_assert(tag::kBool == rt::kTagBool);
static_assert(tag::kInt == rt::kTagInt);
static_assert(tag::kFloat == rt::kTagFloat);
static_assert(tag::kString == rt::kTagString);
static_assert(tag::kColor == rt::kTagColor);
static_assert(tag::kPx == rt::kTagPx);
static_assert(tag::kPct == rt::kTagPct);
static_assert(tag::kFr == rt::kTagFr);
static_assert(tag::kNode == rt::kTagNode);
static_assert(tag::kSeq == rt::kTagSeq);
static_assert(tag::kRecord == rt::kTagRecord);
static_assert(tag::kFunction == rt::kTagFunction);
static_assert(tag::kHandle == rt::kTagHandle);
static_assert(tag::kVoid == rt::kTagVoid);
static_assert(tag::kVariantBit == rt::kVariantBit);
}  // namespace
}  // namespace kura::heap
