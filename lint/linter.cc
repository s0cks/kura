#include "lint/linter.h"

#include <argparse/argparse.hpp>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <jansson.h>
#include <memory>
#include <print>

#include "compiler/frontend/span.h"
#include "compiler/frontend/syntax/diagnostic.h"
#include "compiler/frontend/syntax/source.h"
#include "heap/heap.h"

namespace kura::lint {
using namespace kura::syntax;

static inline auto ToJson(const SourceFile& source, const std::vector<Label>& labels) -> json_t* {
  if (labels.empty())
    return nullptr;

  json_t* l = json_array();
  for (const auto& label : labels)
    json_array_append(l, ToJson(source, label));

  return l;
}

static inline auto ToJson(const std::vector<std::string>& notes) -> json_t* {
  if (notes.empty())
    return nullptr;

  json_t* n = json_array();
  for (const auto& note : notes) {
    json_array_append(n, json_string(note.c_str()));
  }

  return n;
}

[[nodiscard]] static inline auto CodeToJson(const uint64_t rhs) -> json_t* {
  char buffer[8];
  std::snprintf(buffer, sizeof(buffer), "K%04lu", rhs);
  return json_string(buffer);
}

[[nodiscard]] static inline auto ToJson(const SourceFile& source, const Diagnostic& diag) -> json_t* {
  json_t* d = json_object();
  json_object_set(d, "severity", json_string(kura::syntax::ToString(diag.severity).data()));
  json_object_set(d, "code", CodeToJson(diag.code));
  json_object_set(d, "message", json_string(diag.message.c_str()));
  if (!diag.help.empty())
    json_object_set(d, "help", json_string(diag.help.c_str()));
  json_object_set(d, "span_label", json_string(diag.span_label.c_str()));

  json_object_set(d, "span", ToJson(source, diag.span));

  const auto notes = ToJson(diag.notes);
  if (notes)
    json_object_set(d, "notes", notes);

  const auto labels = ToJson(source, diag.labels);
  if (labels)
    json_object_set(d, "labels", labels);
  return d;
}

static inline void PrintJson(std::ostream& stream, const SourceFile& source, const DiagnosticList& diagnostics) {
  json_t* doc = json_object();
  json_object_set(doc, "source", json_string(source.GetName().c_str()));

  json_t* diags = json_array();
  for (const auto& diag : diagnostics)
    json_array_append(diags, ToJson(source, diag));
  json_object_set(doc, "diagnostics", diags);

  stream << json_dumps(doc, JSON_INDENT(2) | JSON_SORT_KEYS) << std::endl;

  json_decref(doc);
}

static inline void PrintJsonl(std::ostream& stream, const SourceFile& source, const DiagnosticList& diagnostics) {
  for (const auto& diag : diagnostics) {
    json_t* doc = ToJson(source, diag);
    json_object_set(doc, "source", json_string(source.GetName().c_str()));

    stream << json_dumps(doc, JSON_COMPACT | JSON_SORT_KEYS) << std::endl;
    json_decref(doc);
  }
}

static inline auto GetLastErrorCodePath() -> std::filesystem::path {
  namespace fs = std::filesystem;

  const auto current_workdir = fs::weakly_canonical(fs::current_path());
  const auto hash = std::hash<std::string>{}(current_workdir.string());

  fs::path cache_dir;
  if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg) {
    cache_dir = fs::path(xdg) / "kura";
  } else if (const char* home = std::getenv("HOME"); home && *home) {
    cache_dir = fs::path(home) / ".cache" / "kura";
  } else {
    cache_dir = fs::temp_directory_path() / "kura";
  }

  std::error_code ec;
  fs::create_directories(cache_dir, ec);

  return cache_dir / std::format("last_{:016x}.txt", hash);
}

static inline auto CodeToString(const uint64_t code) -> std::string {
  char buffer[8];
  std::snprintf(buffer, sizeof(buffer), "K%04lu", code);
  return buffer;
}

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

static inline void WriteDiagnosticsToLast(const std::filesystem::path& p, const SourceFile& source,
                                          const DiagnosticList& rhs) {
  std::ofstream file(p);
  if (file.is_open()) {
    for (const auto& d : rhs) {
      auto* doc = ToJson(source, d);

      const auto primary_start = source.GetPosition(d.span.begin);
      json_object_set_new(doc, "line", json_integer(primary_start.line + 1));
      json_object_set_new(doc, "character", json_integer(primary_start.column + 1));

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

      json_t* j_marks = json_array();
      uint32_t max_line = primary_start.line;
      uint32_t previous_line = UINT32_MAX;

      for (const auto& mark : marks) {
        const auto start = source.GetPosition(mark.span.begin);
        max_line = std::max(max_line, start.line);

        const bool has_gap = (previous_line != UINT32_MAX && start.line != previous_line);
        const bool skipped_lines = (previous_line != UINT32_MAX && start.line > previous_line + 1);
        const bool same_line = (previous_line == start.line);
        previous_line = start.line;

        const auto line_span = source.GetLineSpan(start.line);
        const auto line = ExpandLine(source.GetText(line_span), 2);
        const auto number = std::to_string(start.line + 1);

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

        json_t* j_mark = json_object();
        json_object_set_new(j_mark, "line", json_integer(start.line + 1));
        json_object_set_new(j_mark, "number", json_string(number.c_str()));
        json_object_set_new(j_mark, "same_line", json_boolean(same_line));
        json_object_set_new(j_mark, "has_gap", json_boolean(has_gap));
        json_object_set_new(j_mark, "skipped_lines", json_boolean(skipped_lines));
        if (!same_line) {
          json_object_set_new(j_mark, "line_text", json_string(line.text.c_str()));
        }
        json_object_set_new(j_mark, "from", json_integer(from));
        json_object_set_new(j_mark, "to", json_integer(to));
        json_object_set_new(j_mark, "marker", json_string(marker.c_str()));
        json_object_set_new(j_mark, "text", json_string(mark.text->c_str()));
        json_object_set_new(j_mark, "primary", json_boolean(mark.primary));
        json_array_append_new(j_marks, j_mark);
      }

      if (!d.suggestions.empty()) {
        // Group edits by 0-indexed line number
        std::map<uint32_t, std::vector<TextEdit>> edits_by_line;
        for (const auto& edit : d.suggestions) {
          const auto pos = source.GetPosition(edit.span.begin);
          edits_by_line[pos.line].push_back(edit);
        }

        json_t* j_fix_lines = json_array();
        for (auto& [line_idx, edits] : edits_by_line) {
          const auto line_span = source.GetLineSpan(line_idx);
          std::string patched(source.GetText(line_span));

          // Apply edits on this line in reverse offset order
          std::sort(edits.begin(), edits.end(), [](const TextEdit& a, const TextEdit& b) {
            return a.span.begin > b.span.begin;
          });

          for (const auto& edit : edits) {
            if (edit.span.begin >= line_span.begin && edit.span.end <= line_span.end) {
              const size_t rel_begin = edit.span.begin - line_span.begin;
              const size_t rel_len = edit.span.end - edit.span.begin;
              patched.replace(rel_begin, rel_len, edit.replacement);
            }
          }

          const auto expanded = ExpandLine(patched, 2);
          json_t* j_fline = json_object();
          json_object_set_new(j_fline, "number", json_string(std::to_string(line_idx + 1).c_str()));
          json_object_set_new(j_fline, "text", json_string(expanded.text.c_str()));
          json_array_append_new(j_fix_lines, j_fline);
        }

        json_object_set_new(doc, "fix_lines", j_fix_lines);
      }

      json_object_set_new(doc, "gutter_width", json_integer(std::to_string(max_line + 1).size() + 1));
      json_object_set_new(doc, "marks", j_marks);
      json_object_set(doc, "filename", json_string(source.GetName().c_str()));

      file << json_dumps(doc, JSON_COMPACT) << "\n";

      json_decref(doc);
    }

    file << std::endl;
    file.close();
  }
}

void Linter::Lint(const SyntaxTree& tree) noexcept {
  const auto last = GetLastErrorCodePath();

  const auto& source = tree.GetSource();
  const auto& diagnostics = tree.GetDiagnostics();

  if (diagnostics.empty()) {
    std::error_code ex{};
    std::filesystem::remove(last, ex);
    return;
  }

  WriteDiagnosticsToLast(last, source, diagnostics);

  switch (GetFormat()) {
    case cli::Format::kPlain:
      syntax::Render(std::cout, source, diagnostics, {.color = false, .tab_width = 2});
      break;
    case cli::Format::kPretty:
      syntax::Render(std::cout, source, diagnostics, {.color = true, .tab_width = 2});
      break;
    case cli::Format::kJson:
      PrintJson(std::cout, source, diagnostics);
      break;
    case cli::Format::kJsonl:
      PrintJsonl(std::cout, source, diagnostics);
      break;
  }
}
}  // namespace kura::lint
