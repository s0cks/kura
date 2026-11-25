#ifndef KURA_CLI_FORMAT_H
#define KURA_CLI_FORMAT_H

#include <algorithm>
#include <iostream>
#include <optional>
#include <string_view>

namespace kura::cli {
#define FOR_EACH_CLI_FORMAT(V) \
  V(Plain)                     \
  V(Pretty)                    \
  V(Json)                      \
  V(Jsonl)

enum class Format {
#define DEFINE_FORMAT(Name) k##Name,
  FOR_EACH_CLI_FORMAT(DEFINE_FORMAT)
#undef DEFINE_FORMAT
};

static inline auto operator<<(std::ostream& stream, const Format& rhs) -> std::ostream& {
  switch (rhs) {
#define DEFINE_TOSTRING(Name) \
  case Format::k##Name:       \
    return stream << #Name;

    FOR_EACH_CLI_FORMAT(DEFINE_TOSTRING)
#undef DEFINE_TOSTRING
    default:
      return stream << "Unknown";
  }
}

auto ParseFormat(const std::string_view rhs) noexcept -> std::optional<Format>;
}  // namespace kura::cli

#endif  // KURA_CLI_FORMAT_H
