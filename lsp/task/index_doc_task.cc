#include "lsp/task/index_doc_task.h"

#include <format>

#include "frontend/syntax/ast_printer.h"
#include "log.h"
#include "lsp/diagnostic.h"
#include "lsp/server/server.h"
#include "lsp/types.h"

namespace kura::lsp {
void IndexDocumentTask::DoWork(IndexDocumentTask* ctx) {
  log::info(std::format("indexing {}", ctx->name));
  ctx->tree = syntax::ParseText(ctx->name, ctx->text);

  const auto& tree = ctx->tree;
  const auto& doc = ctx->doc;
  const auto& source = doc->GetSource();

  log::info(std::format("{} total diagnostics", doc->GetTree()->GetDiagnostics().size()));
  for (const auto& d : tree->GetDiagnostics()) {
    const auto start = source.GetPosition(d.span.begin);
    const auto end = source.GetPosition(d.span.end);
    ctx->diagnostics.push_back(Diagnostic{
        .range = Range{start, end},
        .severity = static_cast<Diagnostic::Severity>(d.severity),
        .code = d.code,
        .source = doc->GetUri(),
        .message =
            MarkupContent{
                .kind = MarkupContent::kPlainText,
                .value = d.message,
            },
    });
  }
  log::info(std::format("{} indexed", ctx->name));
}

void IndexDocumentTask::OnFinished(IndexDocumentTask* ctx, int status) {
  if (ctx->tree && ctx->version > ctx->doc->GetTreeVersion())
    ctx->doc->SetSyntaxTree(std::move(ctx->tree), ctx->version);

  log::info(std::format("sending {} diagnostics", ctx->diagnostics.size()));
  ctx->server->Send(ctx->doc->GetUri(), ctx->diagnostics);

  delete ctx;
}
}  // namespace kura::lsp
