#ifndef KURA_DOM_PROPERTY_H
#define KURA_DOM_PROPERTY_H

#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "backend/compile_type.h"
#include "dom/kind.h"

namespace kura {
class Object;

namespace dom {
using TypeMask = uint32_t;

static inline constexpr auto TypeBit(const CompileType::Kind kind) -> TypeMask {
  return TypeMask{1} << static_cast<unsigned>(kind);
}

struct PropertyDef {
  uint16_t index = 0;
  std::string_view name{};
  KindMask kinds = 0;
  TypeMask accepts = 0;
  std::span<const std::string_view> words{};
  double min = -std::numeric_limits<double>::infinity();
  double max = std::numeric_limits<double>::infinity();
  bool event = false;

  inline auto AppliesTo(const Kind kind) const -> bool {
    return (kinds & MaskOf(kind)) != 0;
  }

  inline auto Accepts(const CompileType::Kind kind) const -> bool {
    return (accepts & TypeBit(kind)) != 0;
  }
};

auto AllProperties() -> std::span<const PropertyDef>;
auto FindProperty(Kind kind, std::string_view name) -> const PropertyDef*;
auto PropertiesOf(Kind kind) -> std::vector<const PropertyDef*>;
auto SuggestProperty(Kind kind, std::string_view name) -> const PropertyDef*;
auto DescribeAccepted(const PropertyDef& def) -> std::string;
auto CheckConstant(const PropertyDef& def, Object* value) -> std::optional<std::string>;
auto CheckType(const PropertyDef& def, CompileType* type) -> std::optional<std::string>;
}  // namespace dom
}  // namespace kura

#endif  // KURA_DOM_PROPERTY_H
