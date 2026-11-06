#ifndef KURA_TASK_PARSE_DOC_H
#define KURA_TASK_PARSE_DOC_H

#include <uv.h>

#include "frontend/syntax/parser.h"
#include "lsp/document.h"

namespace kura::lsp {
struct IndexDocumentTask {
  Document* doc;
  std::unique_ptr<syntax::SyntaxTree> tree;

  static void DoWork(IndexDocumentTask* ctx);
  static void OnFinished(IndexDocumentTask* ctx, int status);
};
}  // namespace kura::lsp

#endif  // KURA_TASK_PARSE_DOC_H
