#include "lsp/uri.h"

#include <cctype>

namespace kura::lsp {
namespace {
auto HexValue(const char c) -> int {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

auto IsUnreserved(const unsigned char c) -> bool {
  return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/';
}
}  // namespace

auto UriToPath(const URI& uri) -> std::optional<std::string> {
  constexpr std::string_view kScheme = "file://";
  if (!uri.starts_with(kScheme))
    return std::nullopt;

  std::string path{};
  for (size_t i = kScheme.size(); i < uri.size(); i++) {
    if (uri[i] == '%' && i + 2 < uri.size() && HexValue(uri[i + 1]) >= 0 && HexValue(uri[i + 2]) >= 0) {
      path += static_cast<char>(HexValue(uri[i + 1]) * 16 + HexValue(uri[i + 2]));
      i += 2;
    } else {
      path += uri[i];
    }
  }
  return path;
}

auto PathToUri(const std::string& path) -> URI {
  URI uri = "file://";
  for (const unsigned char c : path) {
    if (IsUnreserved(c)) {
      uri += static_cast<char>(c);
    } else {
      uri += std::format("%{:02X}", c);
    }
  }
  return uri;
}
}  // namespace kura::lsp
