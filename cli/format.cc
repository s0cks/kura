#include "cli/format.h"

namespace kura::cli {
static constexpr auto EqualsIgnoreCase(std::string_view a, std::string_view b) noexcept -> bool {
  if (a.size() != b.size())
    return false;

  for (std::size_t i = 0; i < a.size(); ++i) {
    const auto ca = static_cast<unsigned char>(a[i]);
    const auto cb = static_cast<unsigned char>(b[i]);
    const auto la = (ca >= 'A' && ca <= 'Z') ? (ca + ('a' - 'A')) : ca;
    const auto lb = (cb >= 'A' && cb <= 'Z') ? (cb + ('a' - 'A')) : cb;
    if (la != lb)
      return false;
  }

  return true;
}

auto ParseFormat(const std::string_view rhs) noexcept -> std::optional<Format> {
  if (EqualsIgnoreCase(rhs, "pretty")) [[likely]]
    return Format::kPretty;
  if (EqualsIgnoreCase(rhs, "plain"))
    return Format::kPlain;
  if (EqualsIgnoreCase(rhs, "json"))
    return Format::kJson;
  if (EqualsIgnoreCase(rhs, "jsonl"))
    return Format::kJsonl;

  return std::nullopt;
}
}  // namespace kura::cli
