#include "compiler/frontend/syntax/diagnostic.h"

#include <algorithm>
#include <cstdio>
#include <ostream>
#include <sstream>

namespace kura::syntax {
auto ToString(const DiagnosticCode code) -> std::string {
  char buffer[8];
  std::snprintf(buffer, sizeof(buffer), "K%04lu", code);
  return buffer;
}

static inline auto CodeToString(const uint64_t code) -> std::string {
  char buffer[8];
  std::snprintf(buffer, sizeof(buffer), "K%04lu", code);
  return buffer;
}

auto GetSummary(const DiagnosticCode code) -> std::string_view {
  switch (code) {
#define DEFINE_CASE(Name, Number, Summary) \
  case DiagnosticCode::k##Name:            \
    return Summary;
    FOR_EACH_SYNTAX_DIAGNOSTIC(DEFINE_CASE)
#undef DEFINE_CASE
    default:
      return {};
  }
}

auto ToString(const Severity severity) -> std::string_view {
  switch (severity) {
    case Severity::kError:
      return "ERROR";
    case Severity::kWarning:
      return "WARNING";
    case Severity::kNote:
      return "NOTE";
    case Severity::kStyle:
      return "STYLE";
  }

  return "error";
}

namespace {
constexpr auto kReset = "\033[0m";
constexpr auto kBold = "\033[1m";
constexpr auto kRed = "\033[1;31m";
constexpr auto kYellow = "\033[1;33m";
constexpr auto kBlue = "\033[1;34m";
constexpr auto kCyan = "\033[1;36m";

auto ColorOf(const Severity severity) -> const char* {
  switch (severity) {
    case Severity::kError:
      return kRed;
    case Severity::kWarning:
      return kYellow;
    default:
      return kCyan;
  }
}

// Expands tabs so the carets line up with what the terminal shows, and maps a
// byte column on the line to a display column.
struct Line {
  std::string text{};
  std::vector<uint32_t> display_column{};  // by byte column, plus one past the end
};

auto ExpandLine(std::string_view raw, const uint32_t tab_width) -> Line {
  Line line{};
  uint32_t column = 0;
  for (size_t idx = 0; idx < raw.size(); idx++) {
    line.display_column.push_back(column);
    const auto c = static_cast<unsigned char>(raw[idx]);
    if (c == '\t') {
      const auto spaces = tab_width - (column % tab_width);
      line.text.append(spaces, ' ');
      column += spaces;
      continue;
    }
    line.text.push_back(static_cast<char>(c));
    // continuation bytes of a UTF-8 sequence take no extra column
    if ((c & 0xC0) != 0x80)
      column++;
  }
  line.display_column.push_back(column);
  return line;
}

auto Digits(uint32_t value) -> size_t {
  size_t digits = 1;
  while (value >= 10) {
    value /= 10;
    digits++;
  }
  return digits;
}
}  // namespace

void Render(std::ostream& out, const SourceFile& source, const Diagnostic& d, const RenderOptions& options) {
  const auto paint = [&](const char* color, const std::string& text) {
    return options.color ? std::string(color) + text + kReset : text;
  };

  // gutter width is decided by the largest line number we will print
  auto last_line = source.GetPosition(d.span.begin).line + 1;
  for (const auto& label : d.labels)
    last_line = std::max(last_line, source.GetPosition(label.span.begin).line + 1);
  const auto gutter = std::string(Digits(last_line), ' ');

  const auto begin = source.GetPosition(d.span.begin);
  out << paint(ColorOf(d.severity), std::string(ToString(d.severity)) + "[" + CodeToString(d.code) + "]");
  out << (options.color ? std::string(kBold) + ": " + d.message + kReset : ": " + d.message) << '\n';
  out << gutter << paint(kBlue, "--> ") << source.GetName() << ":" << begin << '\n';
  out << gutter << paint(kBlue, " |") << '\n';

  // The primary span and every label, ordered by line.
  struct Mark {
    Span span;
    const std::string* text;
    bool primary;
  };
  std::vector<Mark> marks{{d.span, &d.span_label, true}};
  for (const auto& label : d.labels)
    marks.push_back({label.span, &label.message, false});
  std::stable_sort(marks.begin(), marks.end(), [](const Mark& a, const Mark& b) {
    return a.span.begin < b.span.begin;
  });

  uint32_t previous_line = UINT32_MAX;
  for (const auto& mark : marks) {
    const auto start = source.GetPosition(mark.span.begin);
    if (previous_line != UINT32_MAX && start.line != previous_line) {
      out << gutter << paint(kBlue, " |") << (start.line > previous_line + 1 ? " ..." : "") << '\n';
    }
    const auto same_line = previous_line == start.line;
    previous_line = start.line;
    const auto line_span = source.GetLineSpan(start.line);
    const auto line = ExpandLine(source.GetText(line_span), options.tab_width);
    const auto number = std::to_string(start.line + 1);
    if (!same_line) {
      out << std::string(gutter.size() - number.size(), ' ') << paint(kBlue, number + " |") << ' ' << line.text << '\n';
    }

    // The carets cover the span on this line; a span that runs on past the
    // end of the line is drawn up to the end of it.
    const auto first = std::min<size_t>(start.column, line.display_column.size() - 1);
    const auto end_offset = std::min(mark.span.end, line_span.end);
    const auto end_column = std::min<size_t>(end_offset - line_span.begin, line.display_column.size() - 1);
    const auto from = line.display_column[first];
    auto to = line.display_column[std::max(first, end_column)];
    if (to <= from)
      to = from + 1;
    const auto marker = std::string(to - from, mark.primary ? '^' : '-');
    out << gutter << paint(kBlue, " |") << ' ' << std::string(from, ' ')
        << paint(mark.primary ? ColorOf(d.severity) : kBlue, marker);
    if (!mark.text->empty())
      out << ' ' << paint(mark.primary ? ColorOf(d.severity) : kBlue, *mark.text);
    out << '\n';
  }

  if (!d.help.empty())
    out << gutter << paint(kBlue, " = ") << paint(kBold, "help: ") << d.help << '\n';
  for (const auto& note : d.notes)
    out << gutter << paint(kBlue, " = ") << paint(kBold, "note: ") << note << '\n';
}

void Render(std::ostream& out, const SourceFile& source, const DiagnosticList& diagnostics,
            const RenderOptions& options) {
  for (const auto& d : diagnostics) {
    Render(out, source, d, options);
    out << '\n';
  }
}

auto RenderToString(const SourceFile& source, const DiagnosticList& diagnostics, const RenderOptions& options)
    -> std::string {
  std::stringstream ss{};
  Render(ss, source, diagnostics, options);
  return ss.str();
}
}  // namespace kura::syntax
