#ifndef KURA_PARSER_H
#define KURA_PARSER_H

#include <cstdint>
#include <ostream>
#include <stdarg.h>
#include <string>

#include "common.h"
#include "lexer.h"

namespace kura {
struct ParseResult {
  bool success = false;
  std::string message{};

  friend auto operator<<(std::ostream& stream, const ParseResult& rhs) -> std::ostream& {
    if (rhs)
      return stream << "Success";
    return stream << rhs.message;
  }

  operator bool() const {
    return success;
  }
};

class Parser {
  DEFINE_NON_COPYABLE_TYPE(Parser);

 private:
  static inline auto Failed(const char* fmt, ...) -> ParseResult {
    va_list args{};
    va_start(args, fmt);

    uint64_t size = 0;
    {
      va_list args_copy{};
      va_copy(args_copy, args);
      size = vsnprintf(nullptr, 0, fmt, args_copy);
      va_end(args_copy);
    }

    if (size <= 0) {
      va_end(args);
      return {.success = false, .message = {}};
    }

    std::string result(size, '\0');
    vsnprintf(&result[0], size + 1, fmt, args);
    va_end(args);
    return {.success = false, .message = result};
  }

 private:
  Lexer lexer_{};

  auto lexer() -> Lexer& {
    return lexer_;
  }

  auto lexer() const -> const Lexer& {
    return lexer_;
  }

 public:
  Parser() = default;
  ~Parser() = default;

 public:
  static void Init();
};
}  // namespace kura

#endif  // KURA_PARSER_H
