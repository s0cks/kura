#include <argparse/argparse.hpp>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <jansson.h>
#include <memory>
#include <print>

#include "frontend/span.h"
#include "heap/heap.h"
#include "linter.h"

static inline auto Slurp(const std::string& filename) -> std::optional<std::string> {
  std::ifstream file(filename, std::ios::in | std::ios::binary);
  if (!file.is_open())
    return std::nullopt;

  return {std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>())};
}

static inline auto ParseTree(const std::string filename) -> std::unique_ptr<kura::syntax::SyntaxTree> {
  const auto contents = Slurp(filename);
  if (!contents)
    return nullptr;

  return kura::syntax::ParseText(filename, *contents);
}

enum class Format {
  kPlain,
  kPretty,
  kJson,
  kJsonl,
};

static inline auto operator<<(std::ostream& stream, const Format& rhs) -> std::ostream& {
  switch (rhs) {
    case Format::kPlain:
      return stream << "plain";
    case Format::kPretty:
      return stream << "pretty";
    case Format::kJson:
      return stream << "json";
    case Format::kJsonl:
      return stream << "jsonl";
  }
}

[[nodiscard]] static inline auto ToJson(const kura::syntax::Position& rhs) -> json_t* {
  json_t* p = json_object();
  json_object_set(p, "line", json_integer(rhs.line));
  json_object_set(p, "character", json_integer(rhs.column));
  return p;
}

[[nodiscard]] static inline auto ToJson(const kura::syntax::SourceFile& source, const kura::Span& span) -> json_t* {
  json_t* s = json_object();
  json_object_set(s, "start", ToJson(source.GetPosition(span.begin)));
  json_object_set(s, "end", ToJson(source.GetPosition(span.end)));
  return s;
}

[[nodiscard]] static inline auto ToJson(const kura::syntax::SourceFile& source, const kura::Label& label) -> json_t* {
  json_t* l = json_object();
  json_object_set(l, "span", ToJson(source, label.span));
  json_object_set(l, "message", json_string(label.message.c_str()));
  return l;
}

static inline auto ToJson(const kura::syntax::SourceFile& source, const std::vector<kura::Label>& labels) -> json_t* {
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

[[nodiscard]] static inline auto ToJson(const kura::syntax::SourceFile& source, const kura::syntax::Diagnostic& diag)
    -> json_t* {
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

static inline void PrintJson(std::ostream& stream, const kura::syntax::SourceFile& source,
                             const kura::syntax::DiagnosticList& diagnostics) {
  json_t* doc = json_object();
  json_object_set(doc, "source", json_string(source.GetName().c_str()));

  json_t* diags = json_array();
  for (const auto& diag : diagnostics)
    json_array_append(diags, ToJson(source, diag));
  json_object_set(doc, "diagnostics", diags);

  stream << json_dumps(doc, JSON_INDENT(2) | JSON_SORT_KEYS) << std::endl;

  json_decref(doc);
}

static inline void PrintJsonl(std::ostream& stream, const kura::syntax::SourceFile& source,
                              const kura::syntax::DiagnosticList& diagnostics) {
  for (const auto& diag : diagnostics) {
    json_t* doc = ToJson(source, diag);
    json_object_set(doc, "source", json_string(source.GetName().c_str()));

    stream << json_dumps(doc, JSON_COMPACT | JSON_SORT_KEYS) << std::endl;
    json_decref(doc);
  }
}

auto main(int argc, char** argv) -> int {
  using namespace kura;
  using namespace argparse;

  ArgumentParser parser("kuralint");
  parser.add_argument("source").help("Specify the source to process");

  auto& color_group = parser.add_mutually_exclusive_group();
  Format format = Format::kPretty;
  // clang-format off
  color_group.add_argument("-f", "--format")
    .default_value("pretty")
    .choices("plain", "pretty", "json", "jsonl")
    .action([&format](const std::string& value) {
      if(value == "plain")
        format = Format::kPlain;
      else if(value == "pretty")
        format = Format::kPretty;
      else if(value == "json")
        format = Format::kJson;
      else if(value == "jsonl")
        format = Format::kJsonl;
    });
  // clang-format on

  try {
    parser.parse_args(argc, argv);
  } catch (const std::exception& err) {
    std::cerr << err.what() << std::endl;
    std::cerr << parser;
    std::exit(1);
  }

  heap::Heap::SetCurrent(new heap::Heap(heap::Heap::SmallOptions()));

  const auto& source = parser.get<std::string>("source");

  syntax::DiagnosticList diagnostics{};
  const auto tree = ParseTree(source);
  if (!tree) {
    std::cerr << "failed to parse: " << source << std::endl;
    std::exit(1);
  }

  for (const auto& diagnostic : tree->GetDiagnostics())
    diagnostics.Add(diagnostic);

  lint::Linter linter{};
  linter.Apply(*tree.get(), diagnostics);

  switch (format) {
    case Format::kPlain:
      syntax::Render(std::cout, tree->GetSource(), diagnostics, {.color = false, .tab_width = 2});
      break;
    case Format::kPretty:
      syntax::Render(std::cout, tree->GetSource(), diagnostics, {.color = true, .tab_width = 2});
      break;
    case Format::kJson:
      PrintJson(std::cout, tree->GetSource(), diagnostics);
      break;
    case Format::kJsonl:
      PrintJsonl(std::cout, tree->GetSource(), diagnostics);
      break;
  }

  return EXIT_SUCCESS;
}
