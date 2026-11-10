#ifndef KURA_TASK_PARSE_DOC_H
#define KURA_TASK_PARSE_DOC_H

#include <cstdint>
#include <string>
#include <uv.h>

#include "frontend/syntax/parser.h"
#include "lsp/document.h"

namespace kura::lsp {
// Parses a snapshot of a document on the thread pool. The task owns a copy of
// the text, so the document can keep changing while the parse runs.
struct IndexDocumentTask {
  Document* doc;
  std::string name{};
  std::string text{};
  uint64_t version = 0;
  std::unique_ptr<syntax::SyntaxTree> tree;

  explicit IndexDocumentTask(Document* d) :
    doc(d),
    name(d->GetUri()),
    text(d->GetContents()),
    version(d->GetVersion()) {}

  static void DoWork(IndexDocumentTask* ctx);
  static void OnFinished(IndexDocumentTask* ctx, int status);
};
}  // namespace kura::lsp

#endif  // KURA_TASK_PARSE_DOC_H
