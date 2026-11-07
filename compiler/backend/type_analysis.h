#ifndef KURA_TYPE_ANALYSIS_H
#define KURA_TYPE_ANALYSIS_H

#include <string>
#include <vector>

#include "backend/compile_type.h"
#include "backend/flow_graph.h"

namespace kura {
class Module;

class TypeAnalysis {
 public:
  static auto Run(Module* module, ModuleGraph* graphs, std::vector<std::string>* errors) -> bool;
};
}  // namespace kura

#endif  // KURA_TYPE_ANALYSIS_H
