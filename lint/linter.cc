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

void Linter::Lint(const SyntaxTree& tree) noexcept {
  const auto& source = tree.GetSource();
  const auto& diagnostics = tree.GetDiagnostics();
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
