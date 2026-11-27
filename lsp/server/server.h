#ifndef KURA_LSP_SERVER_H
#define KURA_LSP_SERVER_H

#include <jansson.h>
#include <string_view>
#include <uv.h>
#include <vector>

#include "lsp/code_action.h"
#include "lsp/diagnostic.h"
#include "lsp/document.h"
#include "lsp/error.h"
#include "lsp/modules.h"
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

class Server {
 private:
  uv_loop_t* loop_;
  uv_pipe_t stdin_{};
  uv_pipe_t stdout_{};
  std::vector<char> buffer_{};
  Router router_{};
  DocumentStore docs_{};
  ModuleResolver modules_{docs_};
  ServerInfo info_{};
  ServerCapabilities caps_{};

  void ProcessRawBytes(const char* data, size_t size);
  void DispatchJsonRpc(std::string_view json);
  void OffloadCompilerTask(json_t* params);
  void IndexDocument(const URI& uri);

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
  void HandleCompletion(const Request& req);
  void HandleDefinition(const Request& req);
  void HandleDocumentHighlight(const Request& req);
  void HandlePrepareRename(const Request& req);
  void HandleRename(const Request& req);
  void HandleFoldingRange(const Request& req);
  void HandleFormatting(const Request& req);
  void HandleRangeFormatting(const Request& req);
  void HandleWorkspaceSymbol(const Request& req);
  void HandleSemanticTokens(const Request& req);
  void HandleSignatureHelp(const Request& req);
  void HandleInlayHint(const Request& req);
  void HandleDocumentLink(const Request& req);
  void HandleIgnored(const Request& req);
  void HandleSemanticTokensFull(const Request& req);

  bool shutdown_requested_ = false;

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

  // The open document for `uri`, or null when it is not (or no longer) open.
  inline auto GetDocument(const URI& uri) -> Document* {
    return docs_.Get(uri);
  }

  void Send(const std::string rhs);
  void Send(const json_t* doc);
  void Send(const Response& resp);
  void Send(const json_int_t req_id, const ResponseType& rhs);
  void Send(const json_int_t req_id, const Error& err);
  void Send(const json_int_t req_id, const std::vector<Symbol>& rhs);
  void Send(const json_int_t req_id, const std::vector<CompletionItem>& rhs);
  void Send(const json_int_t req_id, const std::vector<CodeAction>& rhs);
  void Send(const URI& uri, const std::vector<Diagnostic>& diagnostics);

  // Answers a request with `result`, taking ownership of it. A null `result`
  // is sent as JSON `null`, which is how a request with nothing to say is
  // answered ("no definition here"), as opposed to an error.
  void SendResult(json_int_t req_id, json_t* result);
  inline void SendNull(const json_int_t req_id) {
    SendResult(req_id, json_null());
  }

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
};
}  // namespace kura::lsp

#endif  // KURA_LSP_SERVER_H
