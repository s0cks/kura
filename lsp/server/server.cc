#include "lsp/server/server.h"

#include <charconv>
#include <cstring>
#include <iostream>
#include <jansson.h>
#include <memory>
#include <sstream>
#include <string>
#include <variant>

#include "common.h"
#include "frontend/syntax/ast.h"
#include "log.h"
#include "lsp/error.h"
#include "lsp/hover.h"
#include "lsp/json.h"
#include "lsp/semantic_tokens.h"
#include "lsp/server/params.h"
#include "lsp/server/request.h"
#include "lsp/task/index_doc_task.h"
#include "lsp/task/task.h"
#include "lsp/types.h"

namespace kura::lsp {
Server::Server(uv_loop_t* loop) :
  loop_(loop) {
  KASSERT(loop_);
  info_.name = "kura-lsp";
  info_.version = 1;
  caps_.textDocumentSync = kFullTextDocumentSync;
  caps_.hoverProvider = true;
  caps_.documentSymbolProvider = true;
  caps_.definitionProvider = true;
  caps_.referencesProvider = true;
  caps_.documentHighlightProvider = true;
  caps_.renameProvider = true;
  caps_.foldingRangeProvider = true;
  caps_.workspaceSymbolProvider = true;
  caps_.inlayHintProvider = true;
  caps_.documentLinkProvider = true;
  caps_.signatureHelpTriggerCharacters = {"(", ","};
  caps_.semanticTokensLegend = GetSemanticTokensLegend();
  caps_.documentFormattingProvider = true;
  caps_.documentRangeFormattingProvider = true;
  caps_.codeActionProvider = true;
  caps_.completionProvider.triggerCharacters = {
      ".", ":", "#", "@", "/", "\"",
  };

  uv_os_setenv("UV_THREADPOOL_SIZE", "4");
  uv_pipe_init(loop, &stdin_, 0);
  uv_pipe_init(loop, &stdout_, 0);
  uv_pipe_open(&stdin_, 0);
  uv_pipe_open(&stdout_, 1);

  stdin_.data = this;
  stdout_.data = this;

#define FOR_EACH_ROUTE(V)                           \
  V(Initialize, "initialize")                       \
  V(Initialized, "initialized")                     \
  V(Shutdown, "shutdown")                           \
  V(Exit, "exit")                                   \
  V(DidOpen, "textDocument/didOpen")                \
  V(DidChange, "textDocument/didChange")            \
  V(DidSave, "textDocument/didSave")                \
  V(DidClose, "textDocument/didClose")              \
  V(Hover, "textDocument/hover")                    \
  V(CodeAction, "textDocument/codeAction")          \
  V(CodeActionResolve, "codeAction/resolve")        \
  V(DocumentSymbols, "textDocument/documentSymbol") \
  V(References, "textDocument/references")          \
  V(Completion, "textDocument/completion")          \
  V(Definition, "textDocument/definition")          \
  V(DocumentHighlight, "textDocument/documentHighlight") \
  V(PrepareRename, "textDocument/prepareRename")    \
  V(Rename, "textDocument/rename")                  \
  V(FoldingRange, "textDocument/foldingRange")      \
  V(Formatting, "textDocument/formatting")          \
  V(RangeFormatting, "textDocument/rangeFormatting") \
  V(SignatureHelp, "textDocument/signatureHelp")    \
  V(SemanticTokensFull, "textDocument/semanticTokens/full") \
  V(InlayHint, "textDocument/inlayHint")            \
  V(DocumentLink, "textDocument/documentLink")      \
  V(WorkspaceSymbol, "workspace/symbol")            \
  V(Ignored, "$/cancelRequest")                     \
  V(Ignored, "$/setTrace")

#define MAP(Name, Route)                             \
  router_.Insert(Route, [this](const Request& req) { \
    return Handle##Name(req);                        \
  });

  FOR_EACH_ROUTE(MAP)
#undef MAP
}

Server::~Server() {
  if (loop_)
    uv_loop_close(loop_);
}

void Server::Start() {
  uv_read_start(reinterpret_cast<uv_stream_t*>(&stdin_), &AllocBuffer, &OnRead);
  uv_run(loop_, UV_RUN_DEFAULT);
}

void Server::ProcessRawBytes(const char* data, size_t size) {
  buffer_.insert(buffer_.end(), data, data + size);

  while (!buffer_.empty()) {
    std::string_view window(buffer_.data(), buffer_.size());
    size_t header_end = window.find("\r\n\r\n");
    if (header_end == std::string_view::npos)
      return;

    std::string_view header_part = window.substr(0, header_end);
    size_t cl_pos = header_part.find("Content-Length: ");
    if (cl_pos == std::string_view::npos) {
      buffer_.clear();
      return;
    }

    size_t val_start = cl_pos + 16;
    size_t val_end = header_part.find("\r\n", val_start);
    std::string_view length_str = header_part.substr(val_start, val_end - val_start);

    size_t content_len = 0;
    auto [ptr, ec] = std::from_chars(length_str.data(), length_str.data() + length_str.size(), content_len);
    if (ec != std::errc()) {
      buffer_.clear();
      return;
    }

    size_t total_required_bytes = header_end + 4 + content_len;
    if (buffer_.size() < total_required_bytes)
      return;

    std::string_view payload(buffer_.data() + header_end + 4, content_len);
    DispatchJsonRpc(payload);

    buffer_.erase(buffer_.begin(), buffer_.begin() + total_required_bytes);
  }
}

void Server::OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buff) {
  auto* server = static_cast<Server*>(stream->data);
  if (nread > 0) {
    server->ProcessRawBytes(buff->base, nread);
  } else if (nread < 0) {
    uv_close(reinterpret_cast<uv_handle_t*>(stream), nullptr);
  }

  delete[] buff->base;
}

void Server::Send(const std::string rhs) {
  struct WriteRequest {
    uv_write_t req;
    uv_buf_t buf;
    std::string data;
  };

  auto* req = new WriteRequest();
  req->data = std::move(rhs);
  req->buf = uv_buf_init(req->data.data(), req->data.size());

  uv_write(&req->req, reinterpret_cast<uv_stream_t*>(&stdout_), &req->buf, 1, [](uv_write_t* req, int status) {
    auto* wr = reinterpret_cast<WriteRequest*>(req);
    delete wr;
  });
}

void Server::Send(const json_t* doc) {
  if (!doc)
    return;

  char* raw = json_dumps(doc, JSON_COMPACT);
  if (!raw)
    return;

  std::string_view raw_view(raw);
  std::string frame = "Content-Length: " + std::to_string(raw_view.size()) + "\r\n\r\n";
  frame.append(raw_view);
  Send(frame);
  free(raw);
}

void Server::Send(const Response& resp) {
  JsonResponse json_res{};
  resp >> json_res;
  Send(json_res);
}

void Server::Send(const json_int_t req_id, const Error& err) {
  return Send({
      .id = req_id,
      .value = err,
  });
}

template <typename T>
static inline void SerializeList(const json_int_t req_id, const std::vector<T>& items, JsonResponse& res) {
  json_object_set_new(res, "jsonrpc", json_string("2.0"));
  json_object_set_new(res, "id", json_integer(req_id));

  json_t* result = json_array();
  for (const auto& symbol : items) {
    json_t* sym = json_object();
    symbol >> sym;
    json_array_append_new(result, sym);
  }
  json_object_set_new(res, "result", result);
}

void Server::Send(const URI& uri, const std::vector<Diagnostic>& rhs) {
  json_t* doc = json_object();
  KASSERT(doc);
  json_object_set_new(doc, "jsonrpc", json_string("2.0"));
  json_object_set_new(doc, "method", json_string("textDocument/publishDiagnostics"));

  json_t* params = json_object();
  KASSERT(params);
  {
    json_object_set_new(params, "uri", json_string(uri.c_str()));

    json_t* diagnostics = json_array();
    KASSERT(diagnostics);
    for (const auto& diagnostic : rhs) {
      json_t* value = json_object();
      KASSERT(value);
      diagnostic >> value;
      json_array_append_new(diagnostics, value);
    }
    json_object_set_new(params, "diagnostics", diagnostics);
  }
  json_object_set_new(doc, "params", params);

  return Send(doc);
}

void Server::Send(const json_int_t req_id, const std::vector<CodeAction>& rhs) {
  JsonResponse res{};
  SerializeList(req_id, rhs, res);
  Send(res);
}

void Server::Send(const json_int_t req_id, const std::vector<CompletionItem>& rhs) {
  JsonResponse res{};
  SerializeList(req_id, rhs, res);
  Send(res);
}

void Server::Send(const json_int_t req_id, const std::vector<Symbol>& rhs) {
  JsonResponse res{};
  SerializeList(req_id, rhs, res);
  Send(res);
}

void Server::SendResult(const json_int_t req_id, json_t* result) {
  json_t* doc = json_object();
  json_object_set_new(doc, "jsonrpc", json_string("2.0"));
  json_object_set_new(doc, "id", json_integer(req_id));
  json_object_set_new(doc, "result", result ? result : json_null());
  Send(doc);
  json_decref(doc);
}

void Server::Send(const json_int_t req_id, const ResponseType& rhs) {
  Response res = {
      .id = req_id,
      .value = rhs,
  };
  return Send(res);
}

void Server::DispatchJsonRpc(std::string_view json_msg) {
  Request req(json_msg.data(), json_msg.size());
  if (!req.doc) {
    std::cerr << "LSP Parse Error: " << req.error.text << " at line " << req.error.line << "\n";
    return;
  }

  const auto handler = router_.Search(req.method);
  if (!handler) {
    // Notifications we don't know are ignored, as the protocol says; a
    // request must be answered or the client waits for it forever.
    if (req.has_id && !req.method.empty())
      SendError(req.id, LSP_ERROR_METHOD_NOT_FOUND, std::format("method {} is not supported", req.method));
    return log::info(std::format("no route for {}", req.method));
  }

  (*handler)(req);
}

void Server::IndexDocument(const URI& uri) {
  auto* doc = docs_.Get(uri);
  if (!doc)
    return;

  Queue(new IndexDocumentTask(this, doc));
}

void Server::HandleDidOpen(const Request& req) {
  json_t* doc = json_object_get(req.params, "textDocument");
  if (!doc)
    return;

  TextDocument textDocument{};
  textDocument << doc;
  docs_.Update(textDocument.uri, std::move(textDocument.text));
  log::info(std::format("didOpen: {}", textDocument.uri));
  IndexDocument(textDocument.uri);
}

void Server::HandleDidSave(const Request& req) {
  json_t* doc = json_object_get(req.params, "textDocument");
  if (!doc)
    return;

  TextDocument textDocument{};
  textDocument << doc;
  log::info(std::format("didSave: {}", textDocument.uri));
}

void Server::HandleDidChange(const Request& req) {
  json_t* doc = json_object_get(req.params, "textDocument");
  if (!doc)
    return;

  json_t* contentChanges = json_object_get(req.params, "contentChanges");
  if (!contentChanges)
    return;

  TextDocument textDocument{};
  textDocument << doc;
  log::info(std::format("didChange: {}", textDocument.uri));

  // The server asks for full syncs, so the last change is the whole new text.
  const auto count = json_array_size(contentChanges);
  if (count == 0)
    return;

  const json_t* text_node = json_object_get(json_array_get(contentChanges, count - 1), "text");
  if (!json_is_string(text_node))
    return;

  docs_.Update(textDocument.uri, std::string(json_string_value(text_node)));
  IndexDocument(textDocument.uri);
}

void Server::HandleDidClose(const Request& req) {
  json_t* doc = json_object_get(req.params, "textDocument");
  if (!doc)
    return;

  TextDocument textDocument{};
  textDocument << doc;
  log::info(std::format("didClose: {}", textDocument.uri));
  docs_.Remove(textDocument.uri);
  // A closed file has no problems to show, and the editor keeps what it was
  // last told until it is told otherwise.
  Send(textDocument.uri, std::vector<Diagnostic>{});
}

void Server::HandleHover(const Request& req) {
  TextDocumentPositionParams params{};
  params << req.params;

  auto* doc = docs_.Get(params.textDocument.uri);
  if (!doc)
    return SendNull(req.id);

  const auto hover = GetHover(*doc, params.position, &modules_);
  if (!hover)
    return SendNull(req.id);

  json_t* result = json_object();
  *hover >> result;
  return SendResult(req.id, result);
}

void Server::HandleCodeAction(const Request& req) {
  CodeActionParams params{};
  params << req.params;

  auto* doc = docs_.Get(params.textDocument.uri);
  if (!doc)
    return SendInternalError(req.id, std::format("document {} not indexed", params.textDocument.uri));

  doc->EnsureTree();
  return Send(req.id, GetCodeActions(*doc, params.range));
}

void Server::HandleCodeActionResolve(const Request& req) {
  // TODO(@s0cks): implement
}
}  // namespace kura::lsp
