#ifndef KURA_LSP_JSON_H
#define KURA_LSP_JSON_H

#include <jansson.h>
#include <string>
#include <string_view>

#include "lsp/types.h"

// Small helpers for building responses. Every function returns a NEW
// reference, and `Set`/`Append` take ownership of the value they are given, so
// a handler can write `Set(obj, "range", ToJson(range))` without keeping track
// of reference counts.
namespace kura::lsp::json {
inline auto Str(const std::string_view value) -> json_t* {
  return json_stringn(value.data(), value.size());
}

inline auto Int(const json_int_t value) -> json_t* {
  return json_integer(value);
}

inline void Set(json_t* object, const char* key, json_t* value) {
  json_object_set_new(object, key, value);
}

inline void Append(json_t* array, json_t* value) {
  json_array_append_new(array, value);
}

inline auto ToJson(const Position& pos) -> json_t* {
  json_t* node = json_object();
  Set(node, "line", Int(pos.line));
  Set(node, "character", Int(pos.character));
  return node;
}

inline auto ToJson(const Range& range) -> json_t* {
  json_t* node = json_object();
  Set(node, "start", ToJson(range.start));
  Set(node, "end", ToJson(range.end));
  return node;
}

inline auto ToJson(const TextEdit& edit) -> json_t* {
  json_t* node = json_object();
  Set(node, "range", ToJson(edit.range));
  Set(node, "newText", Str(edit.newText));
  return node;
}

inline auto Location(const std::string_view uri, const Range& range) -> json_t* {
  json_t* node = json_object();
  Set(node, "uri", Str(uri));
  Set(node, "range", ToJson(range));
  return node;
}

// { changes: { [uri]: TextEdit[] } }
inline auto WorkspaceEdit() -> json_t* {
  json_t* node = json_object();
  Set(node, "changes", json_object());
  return node;
}

inline void AddEdit(json_t* workspace_edit, const std::string_view uri, const TextEdit& edit) {
  json_t* changes = json_object_get(workspace_edit, "changes");
  const std::string key(uri);
  json_t* list = json_object_get(changes, key.c_str());
  if (!list) {
    list = json_array();
    json_object_set_new(changes, key.c_str(), list);
  }
  Append(list, ToJson(edit));
}

inline auto GetString(const json_t* object, const char* key, const std::string_view fallback = {}) -> std::string {
  const json_t* value = object ? json_object_get(object, key) : nullptr;
  if (json_is_string(value))
    return json_string_value(value);
  return std::string(fallback);
}

inline auto GetObject(const json_t* object, const char* key) -> json_t* {
  json_t* value = object ? json_object_get(object, key) : nullptr;
  return json_is_object(value) ? value : nullptr;
}

inline auto GetBool(const json_t* object, const char* key, const bool fallback = false) -> bool {
  const json_t* value = object ? json_object_get(object, key) : nullptr;
  return json_is_boolean(value) ? json_is_true(value) : fallback;
}

inline auto GetInt(const json_t* object, const char* key, const json_int_t fallback = 0) -> json_int_t {
  const json_t* value = object ? json_object_get(object, key) : nullptr;
  return json_is_integer(value) ? json_integer_value(value) : fallback;
}
}  // namespace kura::lsp::json

#endif  // KURA_LSP_JSON_H
