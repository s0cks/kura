#include "lsp/task/index_doc_task.h"

#include <format>

#include "frontend/syntax/ast_printer.h"
#include "log.h"

namespace kura::lsp {
void IndexDocumentTask::DoWork(IndexDocumentTask* ctx) {
  log::info(std::format("indexing {}", ctx->doc->GetUri()));
  const auto& name = ctx->doc->GetUri();
  const auto& text = ctx->doc->GetContents();
  ctx->tree = syntax::ParseText(name, text);
}

void IndexDocumentTask::OnFinished(IndexDocumentTask* ctx, int status) {
  if (ctx->tree)
    ctx->doc->SetSyntaxTree(std::move(ctx->tree));
  log::info(std::format("{} indexed", ctx->doc->GetUri()));
}
}  // namespace kura::lsp
