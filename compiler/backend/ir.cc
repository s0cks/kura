#include "ir.h"

#include <cstdlib>
#include <cstring>
#include <sstream>

#include "backend/flow_graph.h"
#include "color.h"
#include "function.h"
#include "gir.h"
#include "heap/color_model.h"
#include "local_scope.h"

namespace kura {
#define DEFINE_ACCEPT(Name)                                   \
  auto Name##Instr::Accept(InstructionVisitor* vis) -> bool { \
    return vis->Visit##Name(this);                            \
  }

FOR_EACH_INSTRUCTION(DEFINE_ACCEPT)
#undef DEFINE_ACCEPT

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

auto ConstantInstr::GetCanonicalKey() const -> std::string {
  switch (value_->GetType()) {
    case kNoneType:
      return "none";
    case kBoolType:
      return (dynamic_cast<Bool*>(value_)->GetValue() ? "bool:1" : "bool:0");
    case kIntType:
      return "int:" + std::to_string(dynamic_cast<Int*>(value_)->GetValue());
    case kFloatType: {
      const double v = dynamic_cast<Float*>(value_)->GetValue();
      uint64_t bits = 0;
      memcpy(&bits, &v, sizeof(bits));
      return "float:" + std::to_string(bits);
    }
    case kStringType:
      return "str:" + std::string(dynamic_cast<String*>(value_)->GetData());

#define DEFINE_CASE(Name)                                                                                   \
  case k##Name##Type: {                                                                                     \
    const auto c = static_cast<Name*>(value_);                                                              \
    const auto& storage = c->GetStorage();                                                                  \
    return std::string{#Name} + ":" + std::to_string(storage.c0) + ":" + std::to_string(storage.c1) + ":" + \
           std::to_string(storage.c2) + ":" + std::to_string(storage.a);                                    \
  }

      FOR_EACH_COLOR_MODEL(DEFINE_CASE)
#undef DEFINE_CASE

    case kPxType: {
      const float v = dynamic_cast<Px*>(value_)->GetValue();
      uint32_t bits = 0;
      memcpy(&bits, &v, sizeof(bits));
      return "px:" + std::to_string(bits);
    }
    default:
      return "ptr:" + std::to_string(reinterpret_cast<uintptr_t>(value_));
  }
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
#define DEFINE_CASE(Name, Tag) \
  case Kind::k##Name:          \
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

auto MathCallInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "MathCall(";
  ss << "op=" << GetOp() << ", ";
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
  std::stringstream ss{};
  ss << "NewRecord(properties=" << GetProperties();
  for (const auto input : GetInputs())
    ss << ", spread=" << Name(input);
  if (IsStackAllocated())
    ss << ", stack";
  ss << ")";
  return ss.str();
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

  if (IsStackAllocated())
    ss << (values.empty() ? "stack" : ", stack");
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

  if (IsStackAllocated())
    ss << ", stack";
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

  if (IsStackAllocated())
    ss << ", stack";
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

auto TracyBeginZoneInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "TracyBeginZoneInstr(";
  ss << "name=" << zone_.name;
  if (!zone_.function.empty())
    ss << ", function=" << zone_.function;
  ss << ")";
  return ss.str();
}

auto TracyEndZoneInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "TracyEndZoneInstr(";
  if (!name_.empty())
    ss << "name=" << name_;
  ss << ")";
  return ss.str();
}

auto RetainInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "RetainInstr(";
  ss << "value=" << Name(GetValue());
  ss << ")";
  return ss.str();
}

auto ReleaseInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "ReleaseInstr(";
  ss << "value=" << Name(GetValue());
  ss << ")";
  return ss.str();
}

auto UpdateRecordInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "UpdateRecordInstr(";
  ss << "base=" << Name(GetBase()) << ", ";
  ss << "entries=" << entries_.size() << ", ";
  ss << "move_base=" << move_base_;
  ss << ")";
  return ss.str();
}

auto SeqAppendInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "SeqAppendInstr(";
  ss << "seq=" << Name(GetSeq()) << ", ";
  ss << "item=" << Name(GetItem());
  ss << ")";
  return ss.str();
}

auto NewColorInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "NewColor(";
  ss << "model=" << GetModel() << ", ";
  ss << "value=" << Name(GetC0()) << ", ";
  ss << "value=" << Name(GetC1()) << ", ";
  ss << "value=" << Name(GetC2()) << ", ";
  ss << "value=" << Name(GetAlpha()) << ", ";
  ss << ")";
  return ss.str();
}

auto CastColorInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "CastColor(";
  ss << "target=" << GetTarget() << ", ";
  ss << "value=" << Name(GetValue());
  ss << ")";
  return ss.str();
}

auto AssertInstr::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Assert(";
  ss << "cond=" << Name(GetCond()) << ", ";
  if (GetMessage())
    ss << "message=" << Name(GetMessage()) << ", ";
  ss << "file=" << file_ << ", ";
  ss << "line=" << line_ << ", ";
  ss << "column=" << column_ << ", ";
  if (has_operands_)
    ss << "left=" << Name(GetLeft()) << ", right=" << Name(GetRight()) << ", ";
  ss << "fail_fast=" << fail_fast_;
  ss << ")";
  return ss.str();
}
}  // namespace kura
