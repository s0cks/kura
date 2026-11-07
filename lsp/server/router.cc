#include "lsp/server/router.h"

#include "log.h"

namespace kura::lsp {
void Router::Insert(std::string_view word, const RouteHandler handler) {
  Route* current = root_.get();
  for (const char c : word) {
    const size_t index = GetPos(c);
    if (index >= 28) [[unlikely]]
      throw std::invalid_argument("Trie only supports character range [a-z]");

    if (!current->children[index])
      current->children[index] = std::make_unique<Route>();

    current = current->children[index].get();
  }

  current->handler = handler;
  current->terminal = true;
}

[[nodiscard]] auto Router::Search(std::string_view word) const noexcept -> std::optional<RouteHandler> {
  const Route* current = root_.get();
  for (const char c : word) {
    const size_t index = GetPos(c);
    if (index >= 28 || !current->children[index])
      return std::nullopt;

    current = current->children[index].get();
  }

  if (current->terminal)
    return {current->handler};

  return std::nullopt;
}
}  // namespace kura::lsp
