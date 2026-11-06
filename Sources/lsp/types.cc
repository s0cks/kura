#include "lsp/types.h"

#include <jansson.h>
#include <variant>

#include "common.h"

namespace kura::lsp {
TextDocument::TextDocument(json_t* doc) {
  KASSERT(doc);
  json_t* uri_node = json_object_get(doc, "uri");
  if (uri_node)
    uri = std::string(json_string_value(uri_node));

  json_t* text_node = json_object_get(doc, "text");
  if (text_node)
    text = std::string(json_string_value(text_node));
}

void ToJson(json_t* doc, const Position& rhs) {
  KASSERT(doc);
  json_object_set(doc, "line", json_integer(rhs.line));
  json_object_set(doc, "character", json_integer(rhs.character));
}

void ToJson(json_t* doc, const Range& rhs) {
  KASSERT(doc);
  json_t* start_node = json_object();
  ToJson(start_node, rhs.start);
  json_object_set(doc, "start", start_node);

  json_t* end_node = json_object();
  ToJson(end_node, rhs.end);
  json_object_set(doc, "end", end_node);
}

void ToJson(json_t* doc, const MarkupContent& rhs) {
  KASSERT(doc);
  json_object_set(doc, "value", json_string(rhs.value.data()));
  switch (rhs.kind) {
    case kura::lsp::MarkupContent::kPlainText:
      json_object_set(doc, "kind", json_string("plaintext"));
      break;
    case kura::lsp::MarkupContent::kMarkdown:
      json_object_set(doc, "kind", json_string("markdown"));
      break;
    default:
      break;
  }
}

void ToJson(json_t* doc, const Hover& rhs) {
  KASSERT(doc);
  json_t* range_node = json_object();
  ToJson(range_node, rhs.range);
  json_object_set(doc, "range", range_node);

  json_t* contents_node = json_object();
  ToJson(contents_node, rhs.contents);
  json_object_set(doc, "contents", contents_node);
}

void FromJson(json_t* doc, Position& rhs) {
  KASSERT(doc);
  json_t* line_node = json_object_get(doc, "line");
  if (line_node)
    rhs.line = json_integer_value(line_node);

  json_t* character_node = json_object_get(doc, "character");
  if (character_node)
    rhs.character = json_integer_value(character_node);
}

void ToJson(json_t* doc, const ServerInfo& rhs) {
  KASSERT(doc);
  json_object_set_new(doc, "name", json_string(rhs.name.data()));
  json_object_set_new(doc, "version", json_integer(rhs.version));
}

void ToJson(json_t* doc, const ServerCapabilities& rhs) {
  KASSERT(doc);
  json_object_set_new(doc, "textDocumentSync", json_integer(rhs.textDocumentSync));

#define SET_PROVIDER_FLAG(Name) \
  if (rhs.Name)                 \
    json_object_set(doc, #Name, json_boolean(rhs.Name));

  FOR_EACH_PROVIDER_FLAG(SET_PROVIDER_FLAG)
#undef SET_PROVIDER_FLAG
}
}  // namespace kura::lsp
