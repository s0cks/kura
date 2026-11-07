#include "server.h"

#include <charconv>
#include <cstring>
#include <iostream>
#include <jansson.h>
#include <memory>
#include <string>
#include <variant>

#include "common.h"
#include "frontend/syntax/ast.h"
#include "log.h"
#include "lsp/error.h"
#include "lsp/hover.h"
#include "lsp/params.h"
#include "lsp/request.h"
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

  uv_os_setenv("UV_THREADPOOL_SIZE", "4");
  uv_pipe_init(loop, &stdin_, 0);
  uv_pipe_init(loop, &stdout_, 0);
  uv_pipe_open(&stdin_, 0);
  uv_pipe_open(&stdout_, 1);

  stdin_.data = this;
  stdout_.data = this;

#define FOR_EACH_ROUTE(V)                \
  V(Initialize, "initialize")            \
  V(Initialized, "initialized")          \
  V(Shutdown, "shutdown")                \
  V(Exit, "exit")                        \
  V(DidOpen, "textDocument/didOpen")     \
  V(DidChange, "textDocument/didChange") \
  V(DidSave, "textDocument/didSave")     \
  V(DidClose, "textDocument/didClose")   \
  V(Hover, "textDocument/hover")

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

  log::info(std::format("searching for {} route...", req.method));
  const auto handler = router_.Search(req.method);
  if (!handler)
    return log::error(std::format("failed to find route for {}", req.method));

  (*handler)(req);
}

void Server::Send(const Response& resp) {
  JsonResponse json_res{};
  resp >> json_res;
  Send(json_res);
}

void Server::WriteError(json_int_t req_id, const Error& err) {
  Response res = {
      .id = req_id,
      .value = err,
  };
  return Send(res);
}

void Server::HandleInitialized(const Request& req) {
  log::info("client initialized");
}

void Server::HandleInitialize(const Request& req) {
  return Send(req.id, InitializeResponse{
                          .info = info_,
                          .capabilities = caps_,
                      });
}

void Server::HandleExit(const Request& req) {
  log::info("exit");
  std::exit(0);
}

void Server::HandleShutdown(const Request& req) {
  log::info("shutdown");
}

void Server::HandleDidOpen(const Request& req) {
  json_t* doc = json_object_get(req.params, "textDocument");
  if (!doc)
    return;

  TextDocument textDocument{};
  textDocument << doc;
  docs_.Update(textDocument.uri, std::move(textDocument.text));
  log::info(std::format("didOpen: {}", textDocument.uri));

  IndexDocumentTask* ctx = new IndexDocumentTask();
  ctx->doc = docs_.Get(textDocument.uri);
  Queue(ctx);
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
}

void Server::HandleDidClose(const Request& req) {
  json_t* doc = json_object_get(req.params, "textDocument");
  if (!doc)
    return;

  TextDocument textDocument{};
  textDocument << doc;
  log::info(std::format("didClose: {}", textDocument.uri));
  docs_.Remove(textDocument.uri);
}

void Server::HandleHover(const Request& req) {
  TextDocumentPositionParams params{};
  params << req.params;

  auto* doc = docs_.Get(params.textDocument.uri);
  if (doc) {
    const auto node = doc->GetNodeAtPos(params.position);

    if (node) {
      std::stringstream ss{};
      if (ToHoverInfo(ss, node))
        return Send(req.id, HoverForRange(ss.str(), params.position, params.position, MarkupContent::kMarkdown));
    }

    return WriteError(req.id, Error{
                                  .code = LSP_ERROR_INTERNAL_ERROR,
                                  .message = "No node found at position",
                              });
  }

  return WriteError(req.id, Error{
                                .code = LSP_ERROR_PARSE,
                                .message = "No parsed document",
                            });
}
}  // namespace kura::lsp
