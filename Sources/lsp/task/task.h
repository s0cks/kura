#ifndef KURA_LSP_TASK_H
#define KURA_LSP_TASK_H

#include <uv.h>

#include "log.h"

namespace kura::lsp {
template <typename C, typename Fn1, typename Fn2>
static inline void Queue(uv_loop_t* loop, Fn1 on_work, Fn2 after_work, C* data) {
  struct WorkContext {
    C* data;
    Fn1 work_fn;
    Fn2 after_fn;
  };

  uv_work_t* work = new uv_work_t();
  work->data = new WorkContext{data, std::move(on_work), std::move(after_work)};

  uv_work_cb on = [](uv_work_t* handle) {
    auto* ctx = static_cast<WorkContext*>(handle->data);
    ctx->work_fn(ctx->data);
  };
  uv_after_work_cb after = [](uv_work_t* handle, int status) {
    auto* ctx = static_cast<WorkContext*>(handle->data);
    ctx->after_fn(ctx->data, status);
    delete ctx;
    delete handle;
  };

  log::debug("queueing work...");
  const int status = uv_queue_work(loop, work, on, after);
  if (status != 0) {
    log::error(std::format("failed to queue work: {}", uv_strerror(status)));
    delete static_cast<WorkContext*>(work->data);
    delete work;
  }
}
}  // namespace kura::lsp

#endif  // KURA_LSP_TASK_H
