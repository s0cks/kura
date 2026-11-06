#include "server.h"

#include <charconv>
#include <cstring>
#include <iostream>
#include <jansson.h>
#include <memory>
#include <string>
#include <variant>

#include "log.h"
#include "lsp/params.h"
#include "lsp/request.h"
#include "lsp/types.h"

namespace kura::lsp {
Server::Server(uv_loop_t* loop) :
  loop_(loop) {
  if (!loop_)
    return;

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

void Server::Send(json_t* doc) {
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

void Server::Send(Response& resp) {
  JsonResponse json_res{};
  ToJson(json_res, resp);
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
  InitializeResponse res{.info =
                             ServerInfo{
                                 .name = "kura-lsp",
                                 .version = 1,
                             },
                         .capabilities = {
                             .textDocumentSync = kFullTextDocumentSync,
                             .hoverProvider = true,
                         }};
  return Send(req.id, res);
}

void Server::HandleExit(const Request& req) {
  log::info("exit");
  std::exit(0);
}

void Server::HandleShutdown(const Request& req) {
  log::info("shutdown");
}

struct IndexTaskContext {
  Server* server_instance;
  std::string uri;
  std::string text_content;
};

void Server::OffloadCompilerTask(json_t* params) {
  auto* context = new IndexTaskContext();
  context->server_instance = this;

  json_t* text_doc = json_object_get(params, "textDocument");
  if (text_doc) {
    json_t* uri_node = json_object_get(text_doc, "uri");
    if (json_is_string(uri_node)) {
      context->uri = json_string_value(uri_node);
    }
  }

  json_t* content_changes = json_object_get(params, "contentChanges");
  if (json_is_array(content_changes) && json_array_size(content_changes) > 0) {
    json_t* first_change = json_array_get(content_changes, 0);
    json_t* text_node = json_object_get(first_change, "text");
    if (json_is_string(text_node)) {
      context->text_content = json_string_value(text_node);
    }
  }

  auto* work_req = new uv_work_t();
  work_req->data = context;

  uv_queue_work(
      loop_, work_req,
      [](uv_work_t* req) {
        auto* ctx = static_cast<IndexTaskContext*>(req->data);
      },
      [](uv_work_t* req, int status) {
        auto* ctx = static_cast<IndexTaskContext*>(req->data);
        delete ctx;
        delete req;
      });
}

void Server::HandleDidOpen(const Request& req) {
  json_t* doc = json_object_get(req.params, "textDocument");
  if (!doc)
    return;

  TextDocument textDocument(doc);
  docs_.Update(textDocument.uri, std::move(textDocument.text));
  log::info(std::format("didOpen: {}", textDocument.uri));
}

void Server::HandleDidSave(const Request& req) {
  json_t* doc = json_object_get(req.params, "textDocument");
  if (!doc)
    return;

  TextDocument textDocument(doc);
  log::info(std::format("didSave: {}", textDocument.uri));
}

void Server::HandleDidChange(const Request& req) {
  json_t* doc = json_object_get(req.params, "textDocument");
  if (!doc)
    return;

  json_t* contentChanges = json_object_get(req.params, "contentChanges");
  if (!contentChanges)
    return;

  TextDocument textDocument(doc);
  log::info(std::format("didChange: {}", textDocument.uri));
}

void Server::HandleDidClose(const Request& req) {
  json_t* doc = json_object_get(req.params, "textDocument");
  if (!doc)
    return;

  TextDocument textDocument(doc);
  log::info(std::format("didClose: {}", textDocument.uri));
  docs_.Remove(textDocument.uri);
}

void Server::HandleHover(const Request& req) {
  TextDocumentPositionParams params{};
  FromJson(req.params, params);

  Hover hover{
      .range =
          Range{
              .start = params.position,
              .end = params.position,
          },
      .contents =
          MarkupContent{
              .kind = MarkupContent::kPlainText,
              .value = "Hello World",
          },
  };
  return Send(req.id, hover);
}
}  // namespace kura::lsp
