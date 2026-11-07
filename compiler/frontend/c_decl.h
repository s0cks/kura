#ifndef KURA_C_DECL_H
#define KURA_C_DECL_H

#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "frontend/type_expr.h"

namespace kura::expr {
class UseDecl {
 private:
  std::string header_;
  std::string alias_;
  DirectiveList directives_;

 public:
  UseDecl(const std::string header, const std::string alias, const DirectiveList directives = {}) :
    header_(std::move(header)),
    alias_(std::move(alias)),
    directives_(std::move(directives)) {}
  ~UseDecl() = default;

  auto GetHeader() const -> const std::string& {
    return header_;
  }

  auto GetAlias() const -> const std::string& {
    return alias_;
  }

  auto GetDirectives() const -> const DirectiveList& {
    return directives_;
  }

  auto ToString() const -> std::string {
    return "use \"" + header_ + "\" as " + alias_;
  }
};

}  // namespace kura::expr

#endif  // KURA_C_DECL_H
