#ifndef KURA_LSP_ERROR_H
#define KURA_LSP_ERROR_H

#include <string>

namespace kura::lsp {
#define LSP_ERROR_PARSE            -32700
#define LSP_ERROR_INVALID_REQUEST  -32600
#define LSP_ERROR_METHOD_NOT_FOUND -32601
#define LSP_ERROR_INVALID_PARAMS   -32602
#define LSP_ERROR_INTERNAL_ERROR   -32603

struct Error {
  int code;
  std::string message;
  void* data = nullptr;
};
}  // namespace kura::lsp

#endif  // KURA_LSP_ERROR_H
