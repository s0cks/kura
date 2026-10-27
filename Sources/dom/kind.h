#ifndef KURA_DOM_KIND_H
#define KURA_DOM_KIND_H

#include <cstdint>
#include <optional>
#include <string_view>

// clang-format off
#define FOR_EACH_ELEMENT_NODE(V)  \
  V(Fragment, "fragment")         \
  V(Block,    "block")            \
  V(Surface,  "surface")          \
  V(List,     "list")             \
  V(Scroll,   "scroll")           \
  V(Button,   "button")           \
  V(Text,     "text")             \
  V(Image,    "image")            \
  V(Input,    "input")            \
  V(Progress, "progress")         \
  V(Spacer,   "spacer")           \
  V(Viewport, "viewport")         \
  V(Canvas,   "canvas")
// clang-format on

namespace kura::dom {
// clang-format off
enum class Kind : uint8_t {
#define DEFINE_KIND(Name, Tag) k##Name,
  FOR_EACH_ELEMENT_NODE(DEFINE_KIND)

#undef DEFINE_KIND
  kDynamic,
  kBind,
  kTotalNumberOfKinds,
};
// clang-format on

using KindMask = uint32_t;

static inline constexpr auto MaskOf(const Kind kind) -> KindMask {
  return KindMask{1} << static_cast<uint8_t>(kind);
}

static inline constexpr auto KindName(const Kind kind) -> std::string_view {
  switch (kind) {
#define DEFINE_CASE(Name, Tag) \
  case Kind::k##Name:          \
    return #Name;

    FOR_EACH_ELEMENT_NODE(DEFINE_CASE)
#undef DEFINE_CASE
    case Kind::kDynamic:
      return "Dynamic";
    case Kind::kBind:
      return "Bind";
    default:
      return "Unknown";
  }
}

static inline constexpr auto KindTag(const Kind kind) -> std::string_view {
  switch (kind) {
#define DEFINE_CASE(Name, Tag) \
  case Kind::k##Name:          \
    return Tag;

    FOR_EACH_ELEMENT_NODE(DEFINE_CASE)
#undef DEFINE_CASE
    default:
      return "";
  }
}

static inline auto FindKind(const std::string_view tag) -> std::optional<Kind> {
#define DEFINE_CASE(Name, Tag) \
  if (tag == Tag)              \
    return Kind::k##Name;

  FOR_EACH_ELEMENT_NODE(DEFINE_CASE)
#undef DEFINE_CASE
  return std::nullopt;
}

static inline constexpr auto IsElement(const Kind kind) -> bool {
  return kind < Kind::kDynamic;
}

static inline constexpr auto CanHaveChildren(const Kind kind) -> bool {
  switch (kind) {
    case Kind::kFragment:
    case Kind::kBlock:
    case Kind::kSurface:
    case Kind::kList:
    case Kind::kScroll:
    case Kind::kButton:
      return true;
    default:
      return false;
  }
}

static inline constexpr auto ReservesSpace(const Kind kind) -> bool {
  return IsElement(kind) && kind != Kind::kFragment;
}
}  // namespace kura::dom

#endif  // KURA_DOM_KIND_H
