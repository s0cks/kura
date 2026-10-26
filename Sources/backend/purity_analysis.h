#ifndef KURA_PURITY_ANALYSIS_H
#define KURA_PURITY_ANALYSIS_H

#include <string>
#include <vector>

#include "backend/flow_graph.h"

namespace kura {
class Module;

// Works out which functions have effects, and rejects a program that has them
// where it mustn't.
//
// A call to C is an effect unless the C function is known to be pure: its
// header says so (`const` or `pure`), or the program says so with `@pure`. A
// function is impure when it reaches an impure C function through the functions
// it calls, the functions it names (a function is a value, too) and the lambdas
// it makes. Recursion is not allowed, so that is a walk over a DAG.
//
// What is rejected:
//   - an impure function reachable from `view`: it must be a pure function of
//     the model;
//   - an impure function reachable from a top-level binding, which runs when
//     the module loads, before a host has set anything up;
//   - a function marked `@pure` that isn't;
//   - an `update` or `init` that doesn't produce the model, and a `view` that
//     doesn't produce a Node. They may call C on the way: the result is the state.
class PurityAnalysis {
 public:
  PurityAnalysis() = delete;
  ~PurityAnalysis() = delete;

  static auto Run(Module* module, ModuleGraph* graphs, std::vector<std::string>* errors) -> bool;
};
}  // namespace kura

#endif  // KURA_PURITY_ANALYSIS_H
