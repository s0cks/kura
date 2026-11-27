#ifndef KURA_LSP_REQUEST_H
#define KURA_LSP_REQUEST_H

#include <jansson.h>
#include <string_view>

namespace kura::lsp {
struct Request {
  json_t* doc;
  json_error_t error;

  json_int_t id = 0;
  // A notification has no id and must not be answered, not even with an error.
  bool has_id = false;
  std::string_view method{};

  json_t* params = nullptr;

  Request(const char* data, const size_t len);
  ~Request();
};
}  // namespace kura::lsp

#endif  // KURA_LSP_REQUEST_H
