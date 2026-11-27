#include "lsp/diagnostic.h"

#include <format>

#include "common.h"

namespace kura::lsp {
auto Diagnostic::GetCodeName() const -> std::string {
  return std::format("K{:04}", code);
}

void Diagnostic::operator>>(json_t* doc) const {
  KASSERT(doc);
  json_t* range_node = json_object();
  range >> range_node;
  json_object_set_new(doc, "range", range_node);

  json_object_set_new(doc, "severity", json_integer(severity));
  json_object_set_new(doc, "code", json_string(GetCodeName().c_str()));
  json_object_set_new(doc, "source", json_string(source ? source->c_str() : "kura"));
  json_object_set_new(doc, "message", json_string(message.c_str()));

  if (!related.empty()) {
    json_t* related_node = json_array();
    for (const auto& r : related) {
      json_t* entry = json_object();
      json_t* location = json_object();
      json_object_set_new(location, "uri", json_string(r.uri.c_str()));
      json_t* location_range = json_object();
      r.range >> location_range;
      json_object_set_new(location, "range", location_range);
      json_object_set_new(entry, "location", location);
      json_object_set_new(entry, "message", json_string(r.message.c_str()));
      json_array_append_new(related_node, entry);
    }
    json_object_set_new(doc, "relatedInformation", related_node);
  }

  // The code action handler matches on this to offer the fix that goes with it.
  json_t* data = json_object();
  json_object_set_new(data, "code", json_integer(static_cast<json_int_t>(code)));
  json_object_set_new(doc, "data", data);
}

auto ToLspSeverity(const syntax::Severity severity) -> Diagnostic::Severity {
  switch (severity) {
    case syntax::Severity::kError:
      return Diagnostic::kError;
    case syntax::Severity::kWarning:
      return Diagnostic::kWarning;
    case syntax::Severity::kStyle:
    case syntax::Severity::kNote:
      return Diagnostic::kHint;
  }
  return Diagnostic::kInformation;
}

auto ToLspMessage(const syntax::Diagnostic& d) -> std::string {
  std::string message = d.message;
  if (!d.span_label.empty() && d.span_label != d.message)
    message += std::format("\n{}", d.span_label);
  if (!d.help.empty())
    message += std::format("\nhelp: {}", d.help);
  for (const auto& note : d.notes)
    message += std::format("\nnote: {}", note);
  if (d.code != 0)
    message += std::format("\nrun `kura explain K{:04}` for more details", d.code);
  return message;
}
}  // namespace kura::lsp
