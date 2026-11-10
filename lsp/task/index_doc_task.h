#ifndef KURA_TASK_PARSE_DOC_H
#define KURA_TASK_PARSE_DOC_H

#include <cstdint>
#include <string>
#include <uv.h>

#include "frontend/syntax/parser.h"
#include "lsp/diagnostic.h"
#include "lsp/document.h"

namespace kura::lsp {
class Server;
struct IndexDocumentTask {
  Server* server;
  Document* doc;
  std::string name{};
  std::string text{};
  uint64_t version = 0;
  std::unique_ptr<syntax::SyntaxTree> tree;
  std::vector<Diagnostic> diagnostics{};

  explicit IndexDocumentTask(Server* s, Document* d) :
    server(s),
    doc(d),
    name(d->GetUri()),
    text(d->GetContents()),
    version(d->GetVersion()) {}

  static void DoWork(IndexDocumentTask* ctx);
  static void OnFinished(IndexDocumentTask* ctx, int status);
};
}  // namespace kura::lsp

#endif  // KURA_TASK_PARSE_DOC_H
