#ifndef KURA_PARSER_H
#define KURA_PARSER_H

#include "common.h"
#include "local_scope.h"
#include "type.h"

namespace kura {
struct Module;
class Parser {
  DEFINE_NON_COPYABLE_TYPE(Parser)
 private:
  LocalScope* scope_;

 public:
  explicit Parser(LocalScope* scope) :
    scope_(scope) {}
  ~Parser() = default;

  auto GetScope() const -> LocalScope* {
    return scope_;
  }

  auto ParseModuleFromFile(const std::string filename, Module** m) -> bool;
};
}  // namespace kura

#endif  // KURA_PARSER_H
