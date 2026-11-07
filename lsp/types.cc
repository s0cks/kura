#include "lsp/types.h"

#include <jansson.h>
#include <variant>

#include "common.h"

namespace kura::lsp {
auto TextDocument::operator<<(json_t* doc) -> TextDocument& {
  KASSERT(doc);
  json_t* uri_node = json_object_get(doc, "uri");
  if (uri_node)
    uri = std::string(json_string_value(uri_node));

  json_t* text_node = json_object_get(doc, "text");
  if (text_node)
    text = std::string(json_string_value(text_node));
  return *this;
}

void Position::operator>>(json_t* doc) const {
  KASSERT(doc);
  json_object_set(doc, "line", json_integer(line));
  json_object_set(doc, "character", json_integer(character));
}

void Range::operator>>(json_t* doc) const {
  KASSERT(doc);
  json_t* start_node = json_object();
  start >> start_node;
  json_object_set(doc, "start", start_node);

  json_t* end_node = json_object();
  end >> end_node;
  json_object_set(doc, "end", end_node);
}

void MarkupContent::operator>>(json_t* doc) const {
  KASSERT(doc);
  json_object_set(doc, "value", json_string(value.data()));
  switch (kind) {
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

void Hover::operator>>(json_t* doc) const {
  KASSERT(doc);
  json_t* range_node = json_object();
  range >> range_node;
  json_object_set(doc, "range", range_node);

  json_t* contents_node = json_object();
  contents >> contents_node;
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

void ServerInfo::operator>>(json_t* doc) const {
  KASSERT(doc);
  json_object_set_new(doc, "name", json_string(name.data()));
  json_object_set_new(doc, "version", json_integer(version));
}

void ServerCapabilities::operator>>(json_t* doc) const {
  KASSERT(doc);
  json_object_set_new(doc, "textDocumentSync", json_integer(textDocumentSync));

#define SET_PROVIDER_FLAG(Name) \
  if (Name)                     \
    json_object_set(doc, #Name, json_boolean(Name));

  FOR_EACH_PROVIDER_FLAG(SET_PROVIDER_FLAG)
#undef SET_PROVIDER_FLAG
}
}  // namespace kura::lsp
