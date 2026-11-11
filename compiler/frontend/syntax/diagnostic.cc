#include "compiler/frontend/syntax/diagnostic.h"

#include <algorithm>
#include <cstdio>
#include <ostream>
#include <rang.hpp>
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
// Expands tabs so the carets line up with what the terminal shows, and maps a
// byte column on the line to a display column.
struct Line {
  std::string text{};
  std::vector<uint32_t> display_column{};  // by byte column, plus one past the end
};

static inline auto ExpandLine(std::string_view raw, const uint32_t tab_width) -> Line {
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

class DiagnosticRenderer {
  std::ostream& stream_;
  const RenderOptions& opts_;

  inline auto stream() -> std::ostream& {
    return stream_;
  }

  inline auto colorize() const -> bool {
    return opts_.color;
  }

#define DEFINE_PAINT_COLOR(Color)                                                 \
  inline auto Color(const std::string_view rhs) -> DiagnosticRenderer& {          \
    if (colorize()) {                                                             \
      stream() << rang::fg::Color << rhs << rang::fg::reset;                      \
    } else {                                                                      \
      stream() << rhs;                                                            \
    }                                                                             \
    return *this;                                                                 \
  }                                                                               \
  inline auto bold_##Color(const std::string_view rhs) -> DiagnosticRenderer& {   \
    if (colorize()) {                                                             \
      stream() << rang::style::bold << rang::fg::Color << rhs << rang::fg::reset; \
    } else {                                                                      \
      stream() << rhs;                                                            \
    }                                                                             \
    return *this;                                                                 \
  }

  DEFINE_PAINT_COLOR(red);
  DEFINE_PAINT_COLOR(blue);
  DEFINE_PAINT_COLOR(green);
  DEFINE_PAINT_COLOR(yellow);
  DEFINE_PAINT_COLOR(cyan);
  DEFINE_PAINT_COLOR(magenta);
  DEFINE_PAINT_COLOR(gray);
#undef DEFINE_PAINT_COLOR

  inline auto paint(const Severity level, const std::string_view rhs) -> DiagnosticRenderer& {
    switch (level) {
      case kura::syntax::Severity::kNote:
        return gray(rhs);
      case kura::syntax::Severity::kError:
        return red(rhs);
      case kura::syntax::Severity::kWarning:
        return yellow(rhs);
      case kura::syntax::Severity::kStyle:
        return cyan(rhs);
    }
  }

  inline auto bold(std::string_view rhs) -> DiagnosticRenderer& {
    if (colorize()) {
      stream() << rang::style::bold << rhs << rang::style::reset;
    } else {
      stream() << rhs;
    }
    return *this;
  }

 public:
  explicit DiagnosticRenderer(std::ostream& stream, const RenderOptions& opts) :
    stream_(stream),
    opts_(opts) {}
  ~DiagnosticRenderer() = default;

  void Render(const SourceFile& source, const Diagnostic& d) {
    // const auto paint = [&](const char* color, const std::string& text) {
    //   return options.color ? std::string(color) + text + kReset : text;
    // };

    // gutter width is decided by the largest line number we will print
    auto last_line = source.GetPosition(d.span.begin).line + 1;
    for (const auto& label : d.labels)
      last_line = std::max(last_line, source.GetPosition(label.span.begin).line + 1);
    const auto gutter = std::string(Digits(last_line), ' ');

    const auto begin = source.GetPosition(d.span.begin);

    paint(d.severity, ToString(d.severity));
    stream() << "[" << CodeToString(d.code) << "] ";
    bold(d.message);
    stream() << "\n";

    magenta("--> ");
    stream() << source.GetName() << ": " << begin << "\n";

    stream() << gutter;
    magenta(" |");
    stream() << "\n";

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
        stream() << gutter;
        magenta(" |");
        stream() << (start.line > previous_line + 1 ? " ..." : "") << '\n';
      }

      const auto same_line = previous_line == start.line;
      previous_line = start.line;
      const auto line_span = source.GetLineSpan(start.line);
      const auto line = ExpandLine(source.GetText(line_span), opts_.tab_width);
      const auto number = std::to_string(start.line + 1);
      if (!same_line) {
        stream() << std::string(gutter.size() - number.size(), ' ');
        magenta(number + " |");
        stream() << " " << line.text << "\n";
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
      stream() << gutter;
      magenta(" |");
      stream() << ' ' << std::string(from, ' ');
      if (mark.primary) {
        paint(d.severity, marker);
      } else {
        magenta(marker);
      }
      stream() << " ";
      if (!mark.text->empty()) {
        if (mark.primary) {
          paint(d.severity, *mark.text);
        } else {
          magenta(*mark.text);
        }
      }
      stream() << '\n';
    }

    if (!d.help.empty()) {
      stream() << gutter;
      magenta(" = ");
      bold("help: ");
      stream() << d.help << "\n";
    }

    for (const auto& note : d.notes) {
      stream() << gutter;
      magenta(" = ");
      bold("note: ");
      stream() << note << "\n";
    }
  }
};

void Render(std::ostream& out, const SourceFile& source, const Diagnostic& d, const RenderOptions& options) {
  DiagnosticRenderer renderer(out, options);
  renderer.Render(source, d);
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
