#ifndef KURA_TYPE_ID_H
#define KURA_TYPE_ID_H

#include <cstdint>

namespace kura {
#define FOR_EACH_TYPE(V) \
  V(Bool)                \
  V(None)                \
  V(Number)              \
  V(String)              \
  V(Property)            \
  V(Record)              \
  V(Seq)

using TypeId = uint64_t;

// clang-format off
enum BuiltinTypes : TypeId {
  kInvalidType = 0,
#define DEFINE_TYPE(Name) k##Name##Type,
  FOR_EACH_TYPE(DEFINE_TYPE)
#undef DEFINE_TYPE
  kTotalNumberOfBuiltinTypes,
};
// clang-format on
static constexpr const uint64_t kBuiltinTypePadding = 100;
static constexpr const uint64_t kFirstTypeIdAfterBuiltins = kTotalNumberOfBuiltinTypes + kBuiltinTypePadding;

static inline constexpr auto IsBuiltinType(const TypeId id) -> bool {
  return id > 0 && id < kFirstTypeIdAfterBuiltins;
}

static inline constexpr auto IsRuntimeType(const TypeId id) -> bool {
  return id > kFirstTypeIdAfterBuiltins;
}
}  // namespace kura

#endif  // KURA_TYPE_ID_H
