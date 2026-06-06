#include "ir.h"

#include <sstream>

#include "local_scope.h"

namespace kura {
#define DEFINE_ACCEPT(Name)                                   \
  auto Name##Instr::Accept(InstructionVisitor* vis) -> bool { \
    return vis->Visit##Name(this);                            \
  }
FOR_EACH_INSTRUCTION(DEFINE_ACCEPT)
#undef DEFINE_ACCEPT

auto Value::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Value(";
  ss << "defn=" << GetDefinition()->ToString();
  ss << ")";
  return ss.str();
}

auto ConstantInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "ConstantInstr(";
  ss << "value=" << GetValue()->ToString();
  ss << ")";
  return ss.str();
}

auto ReturnInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "ReturnInstr(";
  if (HasValue())
    ss << "value=" << GetValue()->ToString();
  ss << ")";
  return ss.str();
}

auto GraphEntryInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "GraphEntryInstr(";
  ss << "id=" << GetBlockId();
  ss << ")";
  return ss.str();
}

auto TargetEntryInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "TargetEntryInstr(";
  ss << "id=" << GetBlockId();
  ss << ")";
  return ss.str();
}

auto JoinEntryInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "JoinEntryInstr(";
  ss << "id=" << GetBlockId();
  ss << ")";
  return ss.str();
}

auto BranchInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "BranchInstr(";
  ss << "condition=" << GetCondition()->ToString() << ", " << std::endl;
  ss << "then=" << GetThen()->ToString() << ", " << std::endl;
  if (HasElse())
    ss << "else=" << GetElse()->ToString() << ", " << std::endl;
  ss << "join=" << GetJoin()->ToString() << std::endl;
  ss << ")";
  return ss.str();
}

auto GotoInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "GotoIntr(";
  ss << "target=" << GetTarget()->ToString();
  ss << ")";
  return ss.str();
}

auto NewNodeInstr::ToString() const -> std::string {
  return "NewNodeInstr()";
}

auto BinaryOpInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "BinaryOpInstr(";
  ss << "op=" << GetOp() << ", ";
  ss << "left=" << GetLeft()->GetDefinition()->ToString() << ", ";
  ss << "right=" << GetRight()->GetDefinition()->ToString();
  ss << ")";
  return ss.str();
}

auto UnaryOpInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "BinaryOpInstr(";
  ss << "op=" << GetOp() << ", ";
  ss << "value=" << GetValue()->GetDefinition()->ToString();
  ss << ")";
  return ss.str();
}

auto CallInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "CallInstr(";
  ss << "target=" << GetTarget()->ToString();
  if (HasArgs()) {
    ss << "args=[";
    for (auto idx = 0; idx < GetNumberOfArgs(); idx++) {
      ss << GetArgAt(idx)->ToString();
      if (idx < GetNumberOfArgs() - 1)
        ss << ", ";
    }
    ss << "]";
  }
  ss << ")";
  return ss.str();
}

auto PipeCallInstr::ToString() const -> std::string {
  return "PipeCallInstr()";
}

auto EntryInstr::GetLastInstruction() -> Instruction* {
  Instruction* last = this;
  while (last->HasNext())
    last = last->GetNext();
  return last;
}

auto GraphEntryInstr::GetLastInstruction() -> Instruction* {
  return HasTarget() ? GetTarget()->GetLastInstruction() : this;
}

auto NewRecordInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "NewRecordInstr(";
  ss << ")";
  return ss.str();
}

auto StorePropertyInstr::ToString() const -> std::string {
  std::stringstream ss;
  ss << "StorePropertyInstr(";
  ss << "instance=" << GetInstance()->GetDefinition()->ToString() << ", ";
  ss << "property=" << GetProperty()->ToString();
  if (HasValue()) {
    ss << ", value=" << GetValue()->GetDefinition()->ToString();
  }
  ss << ")";
  return ss.str();
}

auto LoadLocalInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "LoadLocal(";
  ss << "local=" << GetLocal()->ToString();
  ss << ")";
  return ss.str();
}

auto NewListInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "NewListInstr(";
  const auto& values = GetInputs();
  for (auto idx = 0; idx < values.size(); idx++) {
    if (!values[idx])
      continue;
    ss << values[idx]->ToString();
    if (idx < values.size() - 1)
      ss << ", ";
  }
  ss << ")";
  return ss.str();
}

auto SpreadInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "SpreadInstr(";
  NOT_IMPLEMENTED;  // TODO(@s0cks): implement
  ss << ")";
  return ss.str();
}

auto StoreIndexInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "StoreIndexInstr(";
  NOT_IMPLEMENTED;  // TODO(@s0cks): implement
  ss << ")";
  return ss.str();
}

auto LoadIndexInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "LoadIndexInstr(";
  NOT_IMPLEMENTED;  // TODO(@s0cks): implement
  ss << ")";
  return ss.str();
}

auto LoadPropertyInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "LoadPropertyInstr(";
  NOT_IMPLEMENTED;  // TODO(@s0cks): implement
  ss << ")";
  return ss.str();
}

auto LoadPropertySafelyInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "LoadPropertySafelyInstr(";
  NOT_IMPLEMENTED;  // TODO(@s0cks): implement
  ss << ")";
  return ss.str();
}

auto LoadFunctionInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "LoadFunctionInstr(";
  NOT_IMPLEMENTED;  // TODO(@s0cks): implement
  ss << ")";
  return ss.str();
}

auto ParameterInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "ParameterInstr(";
  NOT_IMPLEMENTED;  // TODO(@s0cks): implement
  ss << ")";
  return ss.str();
}

auto PhiInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "PhiInstr(";
  NOT_IMPLEMENTED;  // TODO(@s0cks): implement
  ss << ")";
  return ss.str();
}

auto StoreLocalInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "StoreLocalInstr(";
  NOT_IMPLEMENTED;  // TODO(@s0cks): implement
  ss << ")";
  return ss.str();
}

auto AllocInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "AllocInstr(";
  NOT_IMPLEMENTED;  // TODO(@s0cks): implement
  ss << ")";
  return ss.str();
}

auto LabelInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "LabelInstr(";
  if (HasLabel())
    ss << "label=" << GetLabel()->ToString();
  ss << ")";
  return ss.str();
}
}  // namespace kura
