#ifndef KURA_TYPE_H
#define KURA_TYPE_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "common.h"

namespace kura {
namespace expr {
class Expr;
class ExprBuilder;
}  // namespace expr

#define FOR_EACH_TYPE(V) \
  V(Bool)                \
  V(None)                \
  V(Number)              \
  V(String)              \
  V(Seq)                 \
  V(Record)              \
  V(Property)

// clang-format off
typedef uint64_t TypeId;
enum BuiltinTypes : TypeId {
  kInvalidType = 0,
#define DEFINE_TYPE(Name) k##Name##Type,
  FOR_EACH_TYPE(DEFINE_TYPE)
#undef DEFINE_TYPE
  kTotalNumberOfBuiltinTypes,
};
// clang-format on
}  // namespace kura

#endif  // KURA_TYPE_H
