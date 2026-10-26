#include "ir.h"

#include <sstream>

#include "backend/flow_graph.h"
#include "function.h"
#include "gir.h"
#include "local_scope.h"

namespace kura {
#define DEFINE_ACCEPT(Name)                                   \
  auto Name##Instr::Accept(InstructionVisitor* vis) -> bool { \
    return vis->Visit##Name(this);                            \
  }
FOR_EACH_INSTRUCTION(DEFINE_ACCEPT)
#undef DEFINE_ACCEPT

// Printed IR names definitions `%id` and blocks `B<id>`; instructions refer to
// their inputs by name only, so a dump stays one line per instruction.
static inline auto Name(const Value* value) -> std::string {
  if (!value || !value->GetDefinition())
    return "<none>";
  return "%" + std::to_string(value->GetDefinition()->GetId());
}

static inline auto Name(const EntryInstr* block) -> std::string {
  if (!block)
    return "<none>";
  return "B" + std::to_string(block->GetBlockId());
}

static inline auto Name(const LocalVariable* local) -> std::string {
  if (!local || !local->GetName())
    return "<local>";
  return std::string(local->GetName()->GetData());
}

auto Value::ToString() const -> std::string {
  return Name(this);
}

auto ConstantInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Constant(" << (GetValue() ? GetValue()->ToString() : "null") << ")";
  return ss.str();
}

auto ReturnInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Return(";
  if (HasValue())
    ss << Name(GetValue());
  ss << ")";
  return ss.str();
}

auto GraphEntryInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "GraphEntry(block_id=" << GetBlockId() << ")";
  return ss.str();
}

auto TargetEntryInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "TargetEntry(block_id=" << GetBlockId() << ")";
  return ss.str();
}

auto JoinEntryInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "JoinEntry(block_id=" << GetBlockId() << ")";
  return ss.str();
}

auto BranchInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Branch(" << Name(GetCondition()) << ", then=" << Name(GetThen());
  if (HasElse())
    ss << ", else=" << Name(GetElse());
  ss << ", join=" << Name(GetJoin()) << ")";
  return ss.str();
}

auto GotoInstr::ToString() const -> std::string {
  return "Goto(" + Name(GetTarget()) + ")";
}

auto NewNodeInstr::ToString() const -> std::string {
  switch (GetKind()) {
#define DEFINE_CASE(Name) \
  case Kind::k##Name:     \
    return "NewNode(" #Name ")";
    FOR_EACH_ELEMENT_NODE(DEFINE_CASE)
#undef DEFINE_CASE
    default:
      return "NewNode(Unknown)";
  }
}

auto BinaryOpInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "BinaryOp(" << GetOp() << ", " << Name(GetLeft()) << ", " << Name(GetRight()) << ")";
  return ss.str();
}

auto UnaryOpInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "UnaryOp(" << GetOp() << ", " << Name(GetValue()) << ")";
  return ss.str();
}

auto CallInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Call(" << Name(GetTarget());
  for (const auto& arg : GetArgs())
    ss << ", " << Name(arg);
  ss << ")";
  return ss.str();
}

auto PipeCallInstr::ToString() const -> std::string {
  return "PipeCall()";
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
  return "NewRecord()";
}

auto StorePropertyInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "StoreProperty(" << Name(GetInstance()) << ", " << GetProperty()->GetPropertyName()->GetData() << ", "
     << Name(GetValue()) << ")";
  return ss.str();
}

auto LoadLocalInstr::ToString() const -> std::string {
  return "LoadLocal(" + Name(GetLocal()) + ")";
}

auto StoreLocalInstr::ToString() const -> std::string {
  return "StoreLocal(" + Name(GetLocal()) + ", " + Name(GetValue()) + ")";
}

auto NewListInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "NewList(";
  const auto& values = GetInputs();
  for (auto idx = 0; idx < values.size(); idx++) {
    ss << Name(values[idx]);
    if (idx < values.size() - 1)
      ss << ", ";
  }
  ss << ")";
  return ss.str();
}

auto SpreadInstr::ToString() const -> std::string {
  return "Spread(" + Name(GetInstance()) + ", " + Name(GetValue()) + ")";
}

auto StoreIndexInstr::ToString() const -> std::string {
  return "StoreIndex(" + Name(GetInstance()) + ", " + Name(GetIndex()) + ", " + Name(GetValue()) + ")";
}

auto LoadIndexInstr::ToString() const -> std::string {
  return "LoadIndex(" + Name(GetInstance()) + ", " + Name(GetIndex()) + ")";
}

auto LoadLengthInstr::ToString() const -> std::string {
  return "LoadLength(" + Name(GetInstance()) + ")";
}

auto LoadPropertyInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "LoadProperty(" << Name(GetInstance()) << ", " << GetProperty()->GetPropertyName()->GetData() << ")";
  return ss.str();
}

auto LoadPropertySafelyInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "LoadPropertySafely(" << Name(GetInstance()) << ", " << GetProperty()->GetPropertyName()->GetData() << ")";
  return ss.str();
}

auto LoadFunctionInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "LoadFunction(" << (GetFunction() ? std::string(GetFunction()->GetName()->GetData()) : "<none>") << ")";
  return ss.str();
}

auto ParameterInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << (IsCapture() ? "Capture(" : "Parameter(") << GetIndex() << ", " << Name(GetLocal()) << ")";
  return ss.str();
}

auto NewVariantInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "NewVariant(" << GetVariantName();
  if (HasPayload())
    ss << ", " << Name(GetPayload());
  ss << ")";
  return ss.str();
}

auto TestVariantInstr::ToString() const -> std::string {
  return "TestVariant(" + Name(GetValue()) + ", " + GetVariantName() + ")";
}

auto LoadVariantPayloadInstr::ToString() const -> std::string {
  return "LoadVariantPayload(" + Name(GetValue()) + ", " + GetVariantName() + ")";
}

auto NewClosureInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "NewClosure(" << (GetTarget() ? GetTarget()->GetName() : std::string("<none>"));
  for (const auto& capture : GetCaptures())
    ss << ", " << Name(capture);
  ss << ")";
  return ss.str();
}

auto PhiInstr::Incoming::ToString() const -> std::string {
  return Name(predecessor) + ":" + Name(value);
}

auto PhiInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Phi(";
  for (auto idx = 0; idx < GetNumberOfIncoming(); idx++) {
    ss << GetIncomingAt(idx).ToString();
    if (idx < GetNumberOfIncoming() - 1)
      ss << ", ";
  }
  ss << ")";
  return ss.str();
}

auto AllocInstr::ToString() const -> std::string {
  return "Alloc()";
}

auto LabelInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Label(";
  if (HasLabel())
    ss << GetLabel()->GetData();
  ss << ")";
  return ss.str();
}
}  // namespace kura
