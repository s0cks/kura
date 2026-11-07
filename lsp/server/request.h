#ifndef KURA_LSP_REQUEST_H
#define KURA_LSP_REQUEST_H

#include <jansson.h>
#include <string_view>

namespace kura::lsp {
struct Request {
  json_t* doc;
  json_error_t error;

  json_int_t id;
  std::string_view method;

  json_t* params;

  Request(const char* data, const size_t len);
  ~Request();
};
}  // namespace kura::lsp

#endif  // KURA_LSP_REQUEST_H
