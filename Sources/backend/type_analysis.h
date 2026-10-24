#ifndef KURA_TYPE_ANALYSIS_H
#define KURA_TYPE_ANALYSIS_H

#include <string>
#include <vector>

#include "backend/compile_type.h"
#include "backend/flow_graph.h"

namespace kura {
class Module;

// Gives every definition in a module's flow graphs a CompileType, and reports
// the programs that don't type check.
//
// The pass is a forward dataflow over each graph in reverse postorder, repeated
// until the types stop changing (loop-carried phis only settle after a second
// visit). Function signatures come from the annotations; a function without a
// return annotation has its return type inferred, and a lambda's parameters are
// inferred from where it is used (`apply((a, b) => a + b, 1, 2)` types `a` and
// `b` from `apply`'s parameter), or are Any when there's nothing to infer them
// from.
//
// Numbers don't mix implicitly: Int op Int is an Int, Float op Float is a
// Float, and Int op Float is an error. `Number` (an Int or a Float, unknown
// which) combines with either.
class TypeAnalysis {
 public:
  // Types the graphs of `module`. Returns false, with a message per problem in
  // `errors`, if it doesn't type check.
  static auto Run(Module* module, ModuleGraph* graphs, std::vector<std::string>* errors) -> bool;
};
}  // namespace kura

#endif  // KURA_TYPE_ANALYSIS_H
