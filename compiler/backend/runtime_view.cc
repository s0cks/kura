// What compiled `init`, `update` and `view` do with the view they are given (see kura::View): they run inside it.
#include "backend/runtime_abi.h"
#include "view.h"

extern "C" {
void kura_rt_view_enter(void* view) {
  kura::View::Enter(static_cast<kura::View*>(view));
}

void kura_rt_view_leave() {
  kura::View::Leave();
}

uint64_t kura_rt_view_depth() {
  return kura::View::Depth();
}

void kura_rt_view_unwind(const uint64_t depth) {
  kura::View::Unwind(depth);
}
}
