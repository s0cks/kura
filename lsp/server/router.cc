#include "lsp/server/router.h"

namespace kura::lsp {
void Router::Insert(const std::string_view word, RouteHandler handler) {
  routes_.insert_or_assign(std::string(word), std::move(handler));
}

[[nodiscard]] auto Router::Search(const std::string_view word) const noexcept -> std::optional<RouteHandler> {
  const auto pos = routes_.find(word);
  if (pos == routes_.end())
    return std::nullopt;
  return pos->second;
}
}  // namespace kura::lsp
