#ifndef KURA_ESCAPE_ANALYSIS_H
#define KURA_ESCAPE_ANALYSIS_H

#include "backend/flow_graph.h"

namespace kura {
// Decides which of a function's allocations can live on its stack.
//
// An allocation that never leaves the function that makes it (it isn't returned, stored somewhere that outlives the
// call, passed to a call, kept for a later frame) and whose size is known where it is made doesn't need the heap: it
// is a block of the function's own stack frame, and goes when the function returns. Everything else is allocated in
// the space of the phase that runs the code (see Phase in runtime_abi.h): the frame's arena in `view`, the model
// space in `init` and `update`.
//
// The analysis is flow-insensitive and one function at a time: anything it can't see through (a call, a store into
// something it doesn't know) is an escape. It marks the allocations that stay with SetStackAllocated; the code
// generator does the rest. What it looks at: NewList, NewRecord (without spreads), NewVariant (with a payload) and
// NewClosure, outside loops, so that one block of stack is never two live objects.
class EscapeAnalysis {
 public:
  EscapeAnalysis() = delete;
  ~EscapeAnalysis() = delete;

  // Analyzes every function of the module. The module's own initializer is left alone: what it makes is constants,
  // and they outlive it.
  static void Run(ModuleGraph* graphs);

  // One function, for tests. Returns how many allocations were left on the stack.
  static auto RunOn(FlowGraph* graph) -> uint64_t;
};
}  // namespace kura

#endif  // KURA_ESCAPE_ANALYSIS_H
