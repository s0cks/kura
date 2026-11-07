#ifndef KURA_LSP_ROUTER_H
#define KURA_LSP_ROUTER_H

#include <cctype>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "lsp/server/request.h"

namespace kura::lsp {
class Router {
 public:
  using RouteHandler = std::function<void(const Request&)>;

 private:
  struct Route {
    std::unique_ptr<Route> children[28];
    bool terminal;
    RouteHandler handler;
  };

  static inline int GetPos(char c) {
    if (c == '/')
      return 27;
    return tolower(c) - 'a';
  }

 private:
  std::unique_ptr<Route> root_;

  inline void ClearNode(std::unique_ptr<Route>& node) {
    if (!node)
      return;

    for (auto& child : node->children) {
      if (child)
        ClearNode(child);
    }

    node.reset();
  }

 public:
  Router() :
    root_(std::make_unique<Route>()) {}
  Router(const Router&) = delete;
  Router(Router&&) noexcept = default;
  ~Router() {
    ClearNode(root_);
  }

  void Insert(std::string_view word, RouteHandler handler);
  [[nodiscard]] std::optional<RouteHandler> Search(std::string_view word) const noexcept;

  Router& operator=(const Router&) = delete;
  Router& operator=(Router&&) noexcept = default;
};
}  // namespace kura::lsp

#endif  // KURA_LSP_ROUTER_H
