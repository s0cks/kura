#include "lsp/task/index_doc_task.h"

#include <format>

#include "frontend/syntax/ast_printer.h"
#include "log.h"

namespace kura::lsp {
void IndexDocumentTask::DoWork(IndexDocumentTask* ctx) {
  log::info("doing work....");
  const auto& name = ctx->doc->GetUri();
  const auto& text = ctx->doc->GetContents();
  ctx->tree = syntax::ParseText(name, text);
}

void IndexDocumentTask::OnFinished(IndexDocumentTask* ctx, int status) {
  log::info(std::format("on finished: {}", status));
  if (ctx->tree) {
    const auto logger = log::FileLogger::Get();
    auto& stream = logger->stream();
    stream << "tree:";
    syntax::PrintTree(stream, *ctx->tree, syntax::PrintOptions{.spans = false});
    stream << std::endl;
    stream.flush();
    ctx->doc->SetSyntaxTree(std::move(ctx->tree));
  }
}
}  // namespace kura::lsp
