#ifndef KURA_PARSER_H
#define KURA_PARSER_H

#include "common.h"
#include "type.h"

namespace kura {
class Parser {
  DEFINE_NON_COPYABLE_TYPE(Parser)
 public:
  Parser() = default;
  ~Parser() = default;

  auto ParseModuleFromFile(const std::string filename, Module** m) -> bool;
};
}  // namespace kura

#endif  // KURA_PARSER_H
