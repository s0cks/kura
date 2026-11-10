#ifndef KURA_LSP_DIAGNOSTIC_H
#define KURA_LSP_DIAGNOSTIC_H

#include "lsp/types.h"

namespace kura::lsp {
struct Diagnostic {
  enum Severity : uint32_t {
    kError = 1,
    kWarning = 2,
    kInformation = 3,
    kHint = 4,
  };

  Range range{};
  Severity severity = kInformation;
  uint64_t code = 0;
  std::optional<std::string> source{};
  MarkupContent message{};

  void operator>>(json_t* doc) const;

#define DEFINE_NEW_DIAGNOSTIC(Level)                                                                                \
  static inline auto Level(const Range range, const uint64_t code, const MarkupContent message) -> Diagnostic {     \
    return Diagnostic{                                                                                              \
        .range = std::move(range),                                                                                  \
        .severity = k##Level,                                                                                       \
        .code = code,                                                                                               \
        .source = std::nullopt,                                                                                     \
        .message = std::move(message),                                                                              \
    };                                                                                                              \
  }                                                                                                                 \
  static inline auto Level(const Range range, const uint64_t code, const MarkupContent message, std::string source) \
      -> Diagnostic {                                                                                               \
    return Diagnostic{                                                                                              \
        .range = std::move(range),                                                                                  \
        .severity = k##Level,                                                                                       \
        .code = code,                                                                                               \
        .source = source,                                                                                           \
        .message = std::move(message),                                                                              \
    };                                                                                                              \
  }

  DEFINE_NEW_DIAGNOSTIC(Error);
  DEFINE_NEW_DIAGNOSTIC(Warning);
  DEFINE_NEW_DIAGNOSTIC(Information);
  DEFINE_NEW_DIAGNOSTIC(Hint);
#undef DEFINE_NEW_DIAGNOSTIC
};
}  // namespace kura::lsp

#endif  // KURA_LSP_DIAGNOSTIC_H
