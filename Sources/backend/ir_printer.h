#ifndef KURA_IR_PRINTER_H
#define KURA_IR_PRINTER_H

#include <ostream>

#include "backend/flow_graph.h"
#include "backend/ir.h"
#include "common.h"

namespace kura {
class IRPrinter {
 private:
  std::ostream& stream_;
  Indent indent_{};

  inline auto stream() const -> std::ostream& {
    return stream_;
  }

  inline auto out() -> std::ostream& {
    return stream() << indent_;
  }

  void PrintInstruction(Instruction* instr);
  void PrintBlock(EntryInstr* block);

 public:
  explicit IRPrinter(std::ostream& stream, const Indent indent = 0) :
    stream_(stream),
    indent_(std::move(indent)) {}
  ~IRPrinter() = default;

  void PrintGraph(FlowGraph* graph);
  void PrintModule(ModuleGraph* module);

 public:
  static inline void Print(std::ostream& stream, FlowGraph* graph, const Indent indent = 0) {
    IRPrinter printer(stream, std::move(indent));
    return printer.PrintGraph(graph);
  }

  static inline void Print(std::ostream& stream, ModuleGraph* module, const Indent indent = 0) {
    IRPrinter printer(stream, std::move(indent));
    return printer.PrintModule(module);
  }
};
}  // namespace kura

#endif  // KURA_IR_PRINTER_H
