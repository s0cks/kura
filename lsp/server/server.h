#ifndef KURA_LSP_SERVER_H
#define KURA_LSP_SERVER_H

#include <jansson.h>
#include <string_view>
#include <uv.h>
#include <vector>

#include "lsp/document.h"
#include "lsp/error.h"
#include "lsp/server/response.h"
#include "lsp/server/router.h"
#include "lsp/task/task.h"
#include "lsp/types.h"

namespace kura::lsp {
template <typename T>
concept Task = requires(T* value, int status) {
  { T::DoWork(value) };
  { T::OnFinished(value, status) };
};

class Server {
  struct JsonResponse {
    json_t* doc;

    JsonResponse() {
      doc = json_object();
    }
    ~JsonResponse() {
      if (doc)
        json_decref(doc);
    }

    auto c_str() const -> const char* {
      return json_dumps(doc, JSON_COMPACT);
    }

    operator json_t*() const {
      return doc;
    }
  };

 private:
  uv_loop_t* loop_;
  uv_pipe_t stdin_{};
  uv_pipe_t stdout_{};
  std::vector<char> buffer_{};
  Router router_{};
  DocumentStore docs_{};
  ServerInfo info_{};
  ServerCapabilities caps_{};

  void Send(const std::string rhs);
  void Send(const json_t* doc);
  void Send(const Response& resp);
  void Send(const json_int_t req_id, const ResponseType& rhs);
  void Send(const json_int_t req_id, const Error& err);
  void Send(const json_int_t req_id, const std::vector<Symbol>& rhs);

  inline void SendError(const json_int_t req_id, const int code, const std::string_view message) {
    return Send(req_id, Error{
                            .code = code,
                            .message = std::string(message),
                        });
  }

  inline void SendParseError(const json_int_t req_id, const std::string_view message = "No parsed document") {
    return SendError(req_id, LSP_ERROR_PARSE, std::move(message));
  }

  inline void SendInternalError(const json_int_t req_id, const std::string_view message = "Internal error") {
    return SendError(req_id, LSP_ERROR_INTERNAL_ERROR, std::move(message));
  }

  void ProcessRawBytes(const char* data, size_t size);
  void DispatchJsonRpc(std::string_view json);
  void OffloadCompilerTask(json_t* params);

  void HandleInitialize(const Request& request);
  void HandleInitialized(const Request& request);
  void HandleShutdown(const Request& request);
  void HandleExit(const Request& request);
  void HandleDidOpen(const Request& req);
  void HandleDidChange(const Request& req);
  void HandleDidClose(const Request& req);
  void HandleDidSave(const Request& req);
  void HandleHover(const Request& req);
  void HandleCodeAction(const Request& req);
  void HandleCodeActionResolve(const Request& req);
  void HandleDocumentSymbols(const Request& req);
  void HandleReferences(const Request& req);

  static inline void AllocBuffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buff) {
    buff->base = new char[suggested_size];
    buff->len = suggested_size;
  }

  static void OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buff);

  template <Task T>
  inline void Queue(T* value) {
    ::kura::lsp::Queue(loop_, T::DoWork, T::OnFinished, value);
  }

 public:
  explicit Server(uv_loop_t* loop);
  ~Server();

  void Start();
};
}  // namespace kura::lsp

#endif  // KURA_LSP_SERVER_H
