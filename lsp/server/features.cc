#include <cctype>
#include <filesystem>
#include <format>
#include <set>

#include "lsp/formatting.h"
#include "lsp/inlay_hints.h"
#include "lsp/json.h"
#include "lsp/semantic_tokens.h"
#include "lsp/signature_help.h"
#include "lsp/navigation.h"
#include "lsp/outline.h"
#include "lsp/server/server.h"

namespace kura::lsp {
namespace {
struct PositionRequest {
  std::string uri{};
  Position position{};
};

auto ParsePositionRequest(const Request& req) -> PositionRequest {
  PositionRequest out{};
  out.uri = json::GetString(json::GetObject(req.params, "textDocument"), "uri");
  if (const auto* position = json::GetObject(req.params, "position"))
    out.position << position;
  return out;
}
}  // namespace

void Server::HandleIgnored(const Request& req) {}

void Server::HandleDefinition(const Request& req) {
  const auto params = ParsePositionRequest(req);
  auto* doc = docs_.Get(params.uri);
  if (!doc)
    return SendNull(req.id);

  const auto target = GetDefinition(*doc, params.position, &modules_);
  if (!target)
    return SendNull(req.id);
  return SendResult(req.id, json::Location(target->uri, target->range));
}

void Server::HandleReferences(const Request& req) {
  const auto params = ParsePositionRequest(req);
  auto* doc = docs_.Get(params.uri);
  if (!doc)
    return SendResult(req.id, json_array());

  const auto include_declaration =
      json::GetBool(json::GetObject(req.params, "context"), "includeDeclaration", true);

  json_t* result = json_array();
  for (const auto& range : GetReferences(*doc, params.position, include_declaration))
    json::Append(result, json::Location(params.uri, range));
  return SendResult(req.id, result);
}

void Server::HandleDocumentHighlight(const Request& req) {
  const auto params = ParsePositionRequest(req);
  auto* doc = docs_.Get(params.uri);
  if (!doc)
    return SendNull(req.id);

  json_t* result = json_array();
  for (const auto& highlight : GetHighlights(*doc, params.position)) {
    json_t* entry = json_object();
    json::Set(entry, "range", json::ToJson(highlight.range));
    json::Set(entry, "kind", json::Int(highlight.kind));
    json::Append(result, entry);
  }
  return SendResult(req.id, result);
}

void Server::HandlePrepareRename(const Request& req) {
  const auto params = ParsePositionRequest(req);
  auto* doc = docs_.Get(params.uri);
  if (!doc)
    return SendNull(req.id);

  const auto target = PrepareRename(*doc, params.position);
  if (!target)
    return SendError(req.id, LSP_ERROR_INVALID_REQUEST, target.error());

  json_t* result = json_object();
  json::Set(result, "range", json::ToJson(target->range));
  json::Set(result, "placeholder", json::Str(target->placeholder));
  return SendResult(req.id, result);
}

void Server::HandleRename(const Request& req) {
  const auto params = ParsePositionRequest(req);
  auto* doc = docs_.Get(params.uri);
  if (!doc)
    return SendNull(req.id);

  const auto edits = Rename(*doc, params.position, json::GetString(req.params, "newName"));
  if (!edits)
    return SendError(req.id, LSP_ERROR_INVALID_REQUEST, edits.error());

  json_t* result = json::WorkspaceEdit();
  for (const auto& edit : *edits)
    json::AddEdit(result, params.uri, edit);
  return SendResult(req.id, result);
}

void Server::HandleFoldingRange(const Request& req) {
  const auto uri = json::GetString(json::GetObject(req.params, "textDocument"), "uri");
  auto* doc = docs_.Get(uri);
  if (!doc)
    return SendResult(req.id, json_array());

  json_t* result = json_array();
  for (const auto& range : GetFoldingRanges(*doc)) {
    json_t* entry = json_object();
    json::Set(entry, "startLine", json::Int(range.start_line));
    json::Set(entry, "endLine", json::Int(range.end_line));
    if (!range.kind.empty())
      json::Set(entry, "kind", json::Str(range.kind));
    json::Append(result, entry);
  }
  return SendResult(req.id, result);
}
}  // namespace kura::lsp

namespace kura::lsp {
namespace {
auto ParseFormattingOptions(const json_t* params) -> FormattingOptions {
  FormattingOptions options{};
  if (const auto* o = json::GetObject(params, "options")) {
    options.tab_size = static_cast<int>(json::GetInt(o, "tabSize", 2));
    options.insert_final_newline = json::GetBool(o, "insertFinalNewline", true);
  }
  return options;
}
}  // namespace

void Server::HandleFormatting(const Request& req) {
  auto* doc = docs_.Get(json::GetString(json::GetObject(req.params, "textDocument"), "uri"));
  if (!doc)
    return SendNull(req.id);

  const auto edits = FormatDocument(*doc, ParseFormattingOptions(req.params));
  if (!edits)
    return SendError(req.id, LSP_ERROR_INVALID_REQUEST, edits.error());

  json_t* result = json_array();
  for (const auto& edit : *edits)
    json::Append(result, json::ToJson(edit));
  return SendResult(req.id, result);
}

void Server::HandleRangeFormatting(const Request& req) {
  auto* doc = docs_.Get(json::GetString(json::GetObject(req.params, "textDocument"), "uri"));
  if (!doc)
    return SendNull(req.id);

  Range range{};
  if (const auto* r = json::GetObject(req.params, "range"))
    range << r;

  const auto edits = FormatDocument(*doc, ParseFormattingOptions(req.params), range);
  if (!edits)
    return SendError(req.id, LSP_ERROR_INVALID_REQUEST, edits.error());

  json_t* result = json_array();
  for (const auto& edit : *edits)
    json::Append(result, json::ToJson(edit));
  return SendResult(req.id, result);
}
}  // namespace kura::lsp

namespace kura::lsp {
void Server::HandleSignatureHelp(const Request& req) {
  const auto params = ParsePositionRequest(req);
  auto* doc = docs_.Get(params.uri);
  if (!doc)
    return SendNull(req.id);

  const auto help = GetSignatureHelp(*doc, params.position, &modules_);
  if (!help)
    return SendNull(req.id);

  json_t* signature = json_object();
  json::Set(signature, "label", json::Str(help->label));
  if (!help->documentation.empty()) {
    json_t* documentation = json_object();
    json::Set(documentation, "kind", json::Str("markdown"));
    json::Set(documentation, "value", json::Str(help->documentation));
    json::Set(signature, "documentation", documentation);
  }
  json_t* parameters = json_array();
  for (const auto& parameter : help->parameters) {
    json_t* entry = json_object();
    json::Set(entry, "label", json::Str(parameter));
    json::Append(parameters, entry);
  }
  json::Set(signature, "parameters", parameters);
  if (help->active_parameter >= 0)
    json::Set(signature, "activeParameter", json::Int(help->active_parameter));

  json_t* signatures = json_array();
  json::Append(signatures, signature);

  json_t* result = json_object();
  json::Set(result, "signatures", signatures);
  json::Set(result, "activeSignature", json::Int(0));
  if (help->active_parameter >= 0)
    json::Set(result, "activeParameter", json::Int(help->active_parameter));
  return SendResult(req.id, result);
}

void Server::HandleSemanticTokensFull(const Request& req) {
  auto* doc = docs_.Get(json::GetString(json::GetObject(req.params, "textDocument"), "uri"));
  if (!doc)
    return SendNull(req.id);

  json_t* data = json_array();
  for (const auto value : GetSemanticTokens(*doc))
    json::Append(data, json::Int(value));

  json_t* result = json_object();
  json::Set(result, "data", data);
  return SendResult(req.id, result);
}

void Server::HandleInlayHint(const Request& req) {
  auto* doc = docs_.Get(json::GetString(json::GetObject(req.params, "textDocument"), "uri"));
  if (!doc)
    return SendResult(req.id, json_array());

  Range range{Position{0, 0}, Position{INT32_MAX, 0}};
  if (const auto* r = json::GetObject(req.params, "range"))
    range << r;

  json_t* result = json_array();
  for (const auto& hint : GetInlayHints(*doc, range)) {
    json_t* entry = json_object();
    json::Set(entry, "position", json::ToJson(hint.position));
    json::Set(entry, "label", json::Str(hint.label));
    json::Set(entry, "kind", json::Int(hint.kind));
    if (hint.padding_left)
      json::Set(entry, "paddingLeft", json_true());
    if (hint.padding_right)
      json::Set(entry, "paddingRight", json_true());
    json::Append(result, entry);
  }
  return SendResult(req.id, result);
}

void Server::HandleDocumentLink(const Request& req) {
  const auto uri = json::GetString(json::GetObject(req.params, "textDocument"), "uri");
  auto* doc = docs_.Get(uri);
  if (!doc || !doc->EnsureTree())
    return SendResult(req.id, json_array());

  json_t* result = json_array();
  for (const auto* item : doc->GetRoot()->items) {
    const auto* use = item->As<syntax::UseDecl>();
    if (!use || !use->has_header)
      continue;

    // `.kui` files are found by their path; a C header only when it sits next to the file.
    std::filesystem::path target = ModuleResolver::ResolvePath(*doc, use->header);
    if (target.empty()) {
      if (const auto from = UriToPath(uri); from && !use->header.starts_with("std/"))
        target = std::filesystem::path(*from).parent_path() / use->header;
    }
    std::error_code ec{};
    if (target.empty() || !std::filesystem::is_regular_file(target, ec))
      continue;

    // The span is of the string with its quotes; the link is what is inside them.
    auto span = use->header_span;
    if (span.end - span.begin >= 2 && doc->GetContents()[span.begin] == '"') {
      span.begin++;
      span.end--;
    }

    json_t* entry = json_object();
    json::Set(entry, "range", json::ToJson(doc->ToRange(span)));
    json::Set(entry, "target", json::Str(PathToUri(target.string())));
    json::Append(result, entry);
  }
  return SendResult(req.id, result);
}

void Server::HandleWorkspaceSymbol(const Request& req) {
  const auto query = json::GetString(req.params, "query");
  const auto matches = [&](const std::string& name) {
    // Every letter of the query, in order, anywhere in the name.
    size_t at = 0;
    for (const auto c : name) {
      if (at < query.size() && std::tolower(static_cast<unsigned char>(c)) == std::tolower(static_cast<unsigned char>(query[at])))
        at++;
    }
    return at == query.size();
  };

  json_t* result = json_array();
  size_t count = 0;
  const auto add = [&](Document& doc) {
    for (const auto& symbol : GetOutline(doc)) {
      if (count >= 200 || !matches(symbol.name))
        continue;
      json_t* entry = json_object();
      json::Set(entry, "name", json::Str(symbol.name));
      json::Set(entry, "kind", json::Int(symbol.kind));
      json::Set(entry, "location", json::Location(doc.GetUri(), symbol.selectionRange));
      json::Append(result, entry);
      count++;
    }
  };

  // Files that are open first, as the editor has them; then the rest of the workspace.
  std::set<std::string> seen{};
  docs_.ForEach([&](Document& doc) {
    seen.insert(doc.GetUri());
    add(doc);
  });
  for (const auto& path : modules_.FindSourceFiles()) {
    if (seen.contains(PathToUri(path.string())))
      continue;
    if (auto* doc = modules_.Load(path))
      add(*doc);
  }
  return SendResult(req.id, result);
}
}  // namespace kura::lsp
