#ifndef KURA_LSP_INITIALIZE_H
#define KURA_LSP_INITIALIZE_H

#include "lsp/types.h"

namespace kura::lsp {
struct InitializeParams {
  int processId = 0;
  std::string rootUri{};
  std::string locale{};
  std::string rootPath{};
  json_t* initializationOptions = nullptr;
  std::vector<WorkspaceFolder> workspaceFolders{};
};

struct InitializeResult {
  ServerInfo info;
  ServerCapabilities capabilities;
  void operator>>(json_t* rhs) const;
};
}  // namespace kura::lsp

#endif  // KURA_LSP_INITIALIZE_H
