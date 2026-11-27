#ifndef KURA_LSP_DIAGNOSTIC_H
#define KURA_LSP_DIAGNOSTIC_H

#include <optional>
#include <string>
#include <vector>

#include "frontend/syntax/diagnostic.h"
#include "lsp/types.h"

namespace kura::lsp {
// Where a diagnostic's secondary labels point.
struct DiagnosticRelated {
  std::string uri{};
  Range range{};
  std::string message{};
};

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
  // Plain text. The base protocol wants a string here; MarkupContent is only
  // allowed by newer clients, and the ones that don't know it show `{...}`.
  std::string message{};
  std::vector<DiagnosticRelated> related{};

  void operator>>(json_t* doc) const;

  // The code as the rest of Kura spells it, `K0034`, which is what
  // `kura explain` takes.
  auto GetCodeName() const -> std::string;

#define DEFINE_NEW_DIAGNOSTIC(Level)                                                                         \
  static inline auto Level(const Range range, const uint64_t code, std::string message) -> Diagnostic {     \
    return Diagnostic{                                                                                       \
        .range = range,                                                                                      \
        .severity = k##Level,                                                                                \
        .code = code,                                                                                        \
        .source = std::nullopt,                                                                              \
        .message = std::move(message),                                                                       \
    };                                                                                                       \
  }

  DEFINE_NEW_DIAGNOSTIC(Error);
  DEFINE_NEW_DIAGNOSTIC(Warning);
  DEFINE_NEW_DIAGNOSTIC(Information);
  DEFINE_NEW_DIAGNOSTIC(Hint);
#undef DEFINE_NEW_DIAGNOSTIC
};

// The LSP severity for a syntax one. Notes and style findings are hints: they
// are suggestions, not problems, and editors draw them accordingly.
auto ToLspSeverity(syntax::Severity severity) -> Diagnostic::Severity;

// What the editor shows for `d`: the message, then `help:` and `note:` lines
// the way the terminal renderer prints them, then the `kura explain` hint.
auto ToLspMessage(const syntax::Diagnostic& d) -> std::string;
}  // namespace kura::lsp

#endif  // KURA_LSP_DIAGNOSTIC_H
