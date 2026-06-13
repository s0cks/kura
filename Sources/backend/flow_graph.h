#ifndef KURA_FLOW_GRAPH_H
#define KURA_FLOW_GRAPH_H

#include "backend/ir.h"

namespace kura {
class FlowGraph {
 private:
  GraphEntryInstr* entry_;

  explicit FlowGraph(GraphEntryInstr* entry) :
    entry_(entry) {}

 public:
  ~FlowGraph() = default;

  auto GetEntry() const -> GraphEntryInstr* {
    return entry_;
  }

  inline auto HasEntry() const -> bool {
    return GetEntry() != nullptr;
  }

 public:
  static inline auto New(GraphEntryInstr* instr) -> FlowGraph* {
    return new FlowGraph(instr);
  }
};
}  // namespace kura

#endif  // KURA_FLOW_GRAPH_H
