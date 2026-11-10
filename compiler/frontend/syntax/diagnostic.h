#ifndef KURA_SYNTAX_DIAGNOSTIC_H
#define KURA_SYNTAX_DIAGNOSTIC_H

#include <algorithm>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "compiler/frontend/syntax/source.h"

namespace kura::syntax {
// Every diagnostic the syntax layer can produce has a stable numeric code, so
// `kurac --explain E0007` (and the LSP) can attach a long form explanation to
// it. Codes are never reused. Name, code, one line summary.
// clang-format off
#define FOR_EACH_SYNTAX_DIAGNOSTIC(V)                                                  \
  V(UnexpectedToken,           1, "unexpected token")                                  \
  V(UnexpectedCharacter,       2, "character that is not part of Kura's syntax")      \
  V(UnterminatedString,        3, "string literal is not terminated")                  \
  V(InvalidEscape,             4, "unknown escape sequence in a string")               \
  V(UnterminatedRawIdent,      5, "backtick name is not terminated")                   \
  V(BadDirective,              6, "'@' is not followed by a directive name")           \
  V(BinaryMinusSpacing,        7, "binary '-' needs whitespace on both sides")         \
  V(IfWithoutElse,             8, "'if' without an 'else'")                            \
  V(FnWithoutParens,           9, "function without a parameter list")                 \
  V(ExternWithoutReturnType,  10, "extern function without a return type")             \
  V(KeywordAsName,            11, "keyword used as a name")                            \
  V(WildcardAsName,           12, "'_' used as a name")                                \
  V(TopLevelExpression,       13, "expression at the top level")                       \
  V(AssignToProperty,         14, "assignment to a property")                          \
  V(BadRecordEntry,           15, "record entry is not 'name: value' or '...spread'")  \
  V(PipeTargetNotCall,        16, "right side of '|>' is not a call")                  \
  V(ForWithoutArrow,          17, "'for' without '=>'")                                \
  V(UseWithoutHeader,         18, "'use' without a header string")                     \
  V(ExpectedItem,             19, "expected a declaration")                            \
  V(MissingClosingDelimiter,  20, "unclosed delimiter")                                \
  V(DuplicateElse,            21, "'else' without a matching 'if'")                    \
  V(EmptyMatch,               22, "'match' without any arms")                          \
  V(TooMuchNesting,           23, "expression is nested too deeply")                 \
  V(BadInterpolation,         24, "string interpolation is malformed")                \
  V(PlaceholderOutsideCall,   25, "'_' placeholder outside a call argument or pipe")  \
  V(MixedPipes,               26, "'|>' and '<|' mixed without parentheses")           \
  V(PositionalNotLast,        27, "positional argument followed by other arguments")  \
  V(BadRecordUpdate,          28, "record update entry is malformed")                 \
  V(WhenWithElse,             29, "'when' with an 'else'")                            \
  V(BadForBinding,            30, "malformed 'for' binding")                          \
  V(BadStaticAssert,          31, "malformed 'static-assert'")
// clang-format on

enum class Severity : uint8_t {
  kError,
  kWarning,
  kNote,
};

enum class DiagnosticCode : uint16_t {
#define DEFINE_CODE(Name, Number, Summary) k##Name = Number,
  FOR_EACH_SYNTAX_DIAGNOSTIC(DEFINE_CODE)
#undef DEFINE_CODE
};

// "E0007"
auto ToString(DiagnosticCode code) -> std::string;
// The one line summary of a code, empty for an unknown one.
auto GetSummary(DiagnosticCode code) -> std::string_view;
auto ToString(Severity severity) -> std::string_view;

// A secondary span: "this `{` is never closed".
struct Label {
  Span span;
  std::string message;
};

struct Diagnostic {
  Severity severity = Severity::kError;
  DiagnosticCode code = DiagnosticCode::kUnexpectedToken;
  std::string message;
  // The span the message is about. Editors underline this.
  Span span;
  // What the span itself should say, e.g. "expected `=>` here".
  std::string span_label;
  std::vector<Label> labels{};
  // A suggestion for how to fix it ("help: ...").
  std::string help;
  // Extra context ("note: ...").
  std::vector<std::string> notes{};

  auto WithLabel(const Span& s, std::string text) -> Diagnostic& {
    labels.push_back({s, std::move(text)});
    return *this;
  }

  auto WithHelp(std::string text) -> Diagnostic& {
    help = std::move(text);
    return *this;
  }

  auto WithNote(std::string text) -> Diagnostic& {
    notes.push_back(std::move(text));
    return *this;
  }

  auto IsError() const -> bool {
    return severity == Severity::kError;
  }
};

class DiagnosticList {
 private:
  std::vector<Diagnostic> diagnostics_{};

 public:
  DiagnosticList() = default;
  ~DiagnosticList() = default;

  auto Add(Diagnostic diagnostic) -> Diagnostic& {
    diagnostics_.push_back(std::move(diagnostic));
    return diagnostics_.back();
  }

  auto Error(const DiagnosticCode code, const Span& span, std::string message, std::string span_label = {})
      -> Diagnostic& {
    Diagnostic d{};
    d.severity = Severity::kError;
    d.code = code;
    d.message = std::move(message);
    d.span = span;
    d.span_label = std::move(span_label);
    return Add(std::move(d));
  }

  // Orders by where in the file the diagnostics are; equal positions keep the
  // order they were reported in.
  void SortBySpan() {
    std::stable_sort(diagnostics_.begin(), diagnostics_.end(), [](const Diagnostic& a, const Diagnostic& b) {
      return a.span.begin < b.span.begin;
    });
  }

  auto empty() const -> bool {
    return diagnostics_.empty();
  }

  auto size() const -> size_t {
    return diagnostics_.size();
  }

  auto HasErrors() const -> bool {
    for (const auto& d : diagnostics_) {
      if (d.IsError())
        return true;
    }
    return false;
  }

  auto at(const size_t idx) const -> const Diagnostic& {
    return diagnostics_.at(idx);
  }

  auto begin() const {
    return diagnostics_.begin();
  }

  auto end() const {
    return diagnostics_.end();
  }

  auto data() const -> const std::vector<Diagnostic>& {
    return diagnostics_;
  }
};

struct RenderOptions {
  bool color = false;
  // How many source lines of context to show around a span is always one: the
  // line the span starts on. Multi line spans are drawn on their first line.
  uint32_t tab_width = 4;
};

// Renders in the style of rustc, with Elm's habit of putting the advice in
// plain words:
//
//   error[E0009]: functions always need a parameter list
//    --> examples/foo.kui:3:7
//     |
//   3 | fn get-pos: Position => { x: 1, }
//     |            ^ expected `(` here
//     = help: write `fn get-pos(): Position => ...`
void Render(std::ostream& out, const SourceFile& source, const Diagnostic& diagnostic,
            const RenderOptions& options = {});
void Render(std::ostream& out, const SourceFile& source, const DiagnosticList& diagnostics,
            const RenderOptions& options = {});
auto RenderToString(const SourceFile& source, const DiagnosticList& diagnostics, const RenderOptions& options = {})
    -> std::string;
}  // namespace kura::syntax

#endif  // KURA_SYNTAX_DIAGNOSTIC_H
