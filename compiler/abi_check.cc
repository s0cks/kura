// Keeps the heap's copy of the value ABI in step with the runtime's. This only includes declarations, so it needs
// nothing beyond the standard library, but it fails the build the moment either side changes a tag or the layout of a
// KuraValue.
#include <cstddef>

#include "backend/runtime_abi.h"
#include "heap/value.h"
#include "runtime/geometry.h"

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

// The geometry the compiler and runtime use is what the renderer ABI passes: the same floats in the same places, so a
// KuraRect is a geom::Rect, and the compiler's vector of four floats is both.
namespace kura::geom {
namespace {
static_assert(sizeof(Vec2) == sizeof(KuraVec2) && alignof(Vec2) == alignof(KuraVec2));
static_assert(offsetof(Vec2, x) == offsetof(KuraVec2, x) && offsetof(Vec2, y) == offsetof(KuraVec2, y));

static_assert(sizeof(Vec3) == sizeof(KuraVec3) && alignof(Vec3) == alignof(KuraVec3));
static_assert(offsetof(Vec3, x) == offsetof(KuraVec3, x) && offsetof(Vec3, y) == offsetof(KuraVec3, y) &&
              offsetof(Vec3, z) == offsetof(KuraVec3, z));

static_assert(sizeof(Rect) == sizeof(KuraRect) && alignof(Rect) == alignof(KuraRect));
static_assert(offsetof(Rect, x) == offsetof(KuraRect, x) && offsetof(Rect, y) == offsetof(KuraRect, y) &&
              offsetof(Rect, width) == offsetof(KuraRect, width) &&
              offsetof(Rect, height) == offsetof(KuraRect, height));

// the generated code builds these as <2 x float>, <3 x float> and <4 x float> and stores them to memory: no padding
static_assert(sizeof(Vec2) == 2 * sizeof(float));
static_assert(sizeof(Vec3) == 3 * sizeof(float));
static_assert(sizeof(Rect) == 4 * sizeof(float));
}  // namespace
}  // namespace kura::geom
