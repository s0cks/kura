#ifndef KURA_PURITY_ANALYSIS_H
#define KURA_PURITY_ANALYSIS_H

#include <string>
#include <vector>

#include "backend/flow_graph.h"

namespace kura {
class Module;

class PurityAnalysis {
 public:
  PurityAnalysis() = delete;
  ~PurityAnalysis() = delete;

  static auto Run(Module* module, ModuleGraph* graphs, std::vector<std::string>* errors) -> bool;
};
}  // namespace kura

#endif  // KURA_PURITY_ANALYSIS_H
