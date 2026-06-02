#ifndef KURA_PARSER_H
#define KURA_PARSER_H

#include <sstream>

#include "common.h"
#include "local_scope.h"
#include "type.h"

namespace kura {
struct Module;
class Parser {
  DEFINE_NON_COPYABLE_TYPE(Parser)
 public:
  struct ParseResult {
    bool success = false;
    std::string message{};

    ParseResult(const bool s, const std::string m) :
      success(s),
      message(std::move(m)) {}
    ~ParseResult() = default;

    operator bool() const {
      return success;
    }

    operator const std::string&() const {
      return message;
    }

    friend auto operator<<(std::ostream& stream, const ParseResult& rhs) -> std::ostream& {
      if (rhs.success)
        return stream << "Success";
      return stream << rhs.message;
    }

    DEFINE_DEFAULT_COPYABLE_TYPE(ParseResult);
  };

  static inline auto Success() -> ParseResult {
    return ParseResult(true, {});
  }

  static inline auto Fail(const std::string rhs) -> ParseResult {
    return ParseResult(false, std::move(rhs));
  }

  static inline auto Fail(const std::string_view rhs) -> ParseResult {
    return Fail(std::string(rhs));
  }

  static inline auto Fail(const std::stringstream& rhs) -> ParseResult {
    return Fail(rhs.str());
  }

 private:
  LocalScope* scope_;

 public:
  explicit Parser(LocalScope* scope) :
    scope_(scope) {}
  ~Parser() = default;

  auto GetScope() const -> LocalScope* {
    return scope_;
  }

  auto ParseModuleFromFile(const std::string filename, Module** m) -> ParseResult;
};
}  // namespace kura

#endif  // KURA_PARSER_H
