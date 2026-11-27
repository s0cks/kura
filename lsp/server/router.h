#ifndef KURA_LSP_ROUTER_H
#define KURA_LSP_ROUTER_H

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "lsp/server/request.h"

namespace kura::lsp {
// Maps a JSON-RPC method name to its handler. Method names are matched
// exactly, as the protocol defines them: they are case sensitive and have
// digits and `$` in them (`$/cancelRequest`).
class Router {
 public:
  using RouteHandler = std::function<void(const Request&)>;

 private:
  struct StringHash {
    using is_transparent = void;
    auto operator()(const std::string_view value) const noexcept -> size_t {
      return std::hash<std::string_view>{}(value);
    }
  };

  std::unordered_map<std::string, RouteHandler, StringHash, std::equal_to<>> routes_{};

 public:
  Router() = default;
  Router(const Router&) = delete;
  Router(Router&&) noexcept = default;
  ~Router() = default;

  void Insert(std::string_view word, RouteHandler handler);
  [[nodiscard]] auto Search(std::string_view word) const noexcept -> std::optional<RouteHandler>;

  Router& operator=(const Router&) = delete;
  Router& operator=(Router&&) noexcept = default;
};
}  // namespace kura::lsp

#endif  // KURA_LSP_ROUTER_H
