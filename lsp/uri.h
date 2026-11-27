#ifndef KURA_LSP_URI_H
#define KURA_LSP_URI_H

#include <format>
#include <optional>
#include <string>

namespace kura::lsp {
using URI = std::string;

// `file:///home/me/a%20b.kui` -> `/home/me/a b.kui`. Nothing for a URI that
// is not a file.
auto UriToPath(const URI& uri) -> std::optional<std::string>;

// The reverse: `/home/me/a b.kui` -> `file:///home/me/a%20b.kui`.
auto PathToUri(const std::string& path) -> URI;
}  // namespace kura::lsp

#endif  // KURA_LSP_URI_H
