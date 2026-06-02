#ifndef KURA_IR_PRINTER_H
#define KURA_IR_PRINTER_H

#include "flow_graph.h"
#include "ir.h"

namespace kura {
class IRPrinter {
 private:
  std::ostream& stream_;
  Indent indent_{};

  inline auto stream() const -> std::ostream& {
    return stream_;
  }

  inline auto indent() const -> const Indent& {
    return indent_;
  }

  inline auto out() -> std::ostream& {
    return stream() << indent();
  }

  inline void line() {
    out() << std::endl;
  }

  void PrintInstruction(Instruction* instr);
  void PrintInstructions(EntryInstr* instr);

 public:
  explicit IRPrinter(std::ostream& stream, const Indent indent = 0) :
    stream_(stream),
    indent_(std::move(indent)) {}
  ~IRPrinter() = default;

  inline void PrintTarget(TargetEntryInstr* instr) {
    return PrintInstructions(instr);
  }

  inline void PrintGraph(GraphEntryInstr* instr) {
    line();
    out() << "HIR:" << std::endl;
    PrintTarget(instr->GetTarget());
    line();
  }

  inline void PrintGraph(FlowGraph* graph) {
    PrintGraph(graph->GetEntry());
  }

 public:
  static inline void Print(std::ostream& stream, TargetEntryInstr* instr, const Indent indent = 0) {
    IRPrinter printer(stream, std::move(indent));
    return printer.PrintTarget(instr);
  }

  static inline void Print(std::ostream& stream, GraphEntryInstr* instr, const Indent indent = 0) {
    IRPrinter printer(stream, std::move(indent));
    return printer.PrintGraph(instr);
  }

  static inline void Print(std::ostream& stream, FlowGraph* graph, const Indent indent = 0) {
    return Print(stream, graph->GetEntry(), std::move(indent));
  }
};
}  // namespace kura

#endif  // KURA_IR_PRINTER_H
