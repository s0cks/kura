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

  // The tree that was just parsed belongs to `text`, so positions come from a
  // source file made of that text, not from whatever the document holds by the
  // time this runs (it may already be newer).
  const auto& tree = ctx->tree;
  const auto& source = tree->GetSource();

  for (const auto& d : tree->GetDiagnostics()) {
    Diagnostic out{
        .range = Range{ToLspPosition(source, d.span.begin), ToLspPosition(source, d.span.end)},
        .severity = ToLspSeverity(d.severity),
        .code = d.code,
        .source = std::string("kura"),
        .message = ToLspMessage(d),
    };

    // Each secondary label becomes a "related information" entry, which
    // editors show as a clickable list under the message.
    for (const auto& label : d.labels) {
      out.related.push_back(DiagnosticRelated{
          .uri = ctx->doc->GetUri(),
          .range = Range{ToLspPosition(source, label.span.begin), ToLspPosition(source, label.span.end)},
          .message = label.message,
      });
    }
    ctx->diagnostics.push_back(std::move(out));
  }
  log::info(std::format("{} indexed, {} diagnostics", ctx->name, ctx->diagnostics.size()));
}

void IndexDocumentTask::OnFinished(IndexDocumentTask* ctx, int status) {
  // The document may have been closed while this ran on a worker thread; the
  // pointer the task was made with is then dangling, so look it up again.
  auto* doc = ctx->server->GetDocument(ctx->name);
  if (doc != ctx->doc) {
    delete ctx;
    return;
  }

  if (ctx->tree && ctx->version > doc->GetTreeVersion())
    doc->SetSyntaxTree(std::move(ctx->tree), ctx->version);

  // Only the newest text's diagnostics are worth showing: when a later edit
  // is already queued, its own run will publish, and this one would flicker.
  if (ctx->version >= doc->GetVersion())
    ctx->server->Send(doc->GetUri(), ctx->diagnostics);

  delete ctx;
}
}  // namespace kura::lsp
