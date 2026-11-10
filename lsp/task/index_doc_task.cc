#include "lsp/task/index_doc_task.h"

#include <format>

#include "frontend/syntax/ast_printer.h"
#include "log.h"

namespace kura::lsp {
void IndexDocumentTask::DoWork(IndexDocumentTask* ctx) {
  log::info(std::format("indexing {}", ctx->name));
  ctx->tree = syntax::ParseText(ctx->name, ctx->text);
}

void IndexDocumentTask::OnFinished(IndexDocumentTask* ctx, int status) {
  // Runs on the loop thread. Parses can finish out of order, so only a tree
  // newer than the one the document already has is kept.
  if (ctx->tree && ctx->version > ctx->doc->GetTreeVersion())
    ctx->doc->SetSyntaxTree(std::move(ctx->tree), ctx->version);
  log::info(std::format("{} indexed", ctx->name));
  delete ctx;
}
}  // namespace kura::lsp
