#ifndef KURA_IR_H
#define KURA_IR_H

#include <functional>
#include <string>

#include "element.h"
#include "expr.h"
#include "object.h"
#include "property.h"

namespace kura {
#define FOR_EACH_INSTRUCTION(V) \
  V(GraphEntry)                 \
  V(TargetEntry)                \
  V(JoinEntry)                  \
  V(Constant)                   \
  V(UnaryOp)                    \
  V(BinaryOp)                   \
  V(NewList)                    \
  V(NewRecord)                  \
  V(NewNode)                    \
  V(Spread)                     \
  V(StoreIndex)                 \
  V(LoadIndex)                  \
  V(StoreProperty)              \
  V(LoadProperty)               \
  V(LoadPropertySafely)         \
  V(LoadFunction)               \
  V(Parameter)                  \
  V(Call)                       \
  V(PipeCall)                   \
  V(Return)                     \
  V(Goto)                       \
  V(Branch)                     \
  V(Phi)                        \
  V(StoreLocal)                 \
  V(LoadLocal)                  \
  V(Alloc)

class Instruction;
class EntryInstr;
#define DECLARE_INSTRUCTION(Name) class Name##Instr;
FOR_EACH_INSTRUCTION(DECLARE_INSTRUCTION);
#undef DECLARE_INSTRUCTION

class InstructionVisitor {
 protected:
  InstructionVisitor() = default;

 public:
  virtual ~InstructionVisitor() = default;
#define DEFINE_VISIT(Name) virtual auto Visit##Name(Name##Instr* instr) -> bool = 0;
  FOR_EACH_INSTRUCTION(DEFINE_VISIT);
#undef DEFINE_VISIT
};

class Definition;
class Value {
 private:
  Definition* defn_;

 public:
  explicit Value(Definition* defn) :
    defn_(defn) {}
  ~Value() = default;

  auto GetDefinition() const -> Definition* {
    return defn_;
  }

  auto ToString() const -> std::string;

 public:
  static inline auto New(Definition* defn) -> Value* {
    return new Value(defn);
  }
};

class InstructionIterator;
class ForwardInstructionIterator;
// TODO(@s0cks): implement BackwardInstructionIterator
class Instruction {
 private:
  Instruction* previous_ = nullptr;
  Instruction* next_ = nullptr;

 protected:
  Instruction() = default;

  void SetPrevious(Instruction* p) {
    previous_ = p;
  }

  void SetNext(Instruction* n) {
    next_ = n;
  }

  virtual void SetInputAt(const uint64_t idx, Value* rhs) {
    // do nothing
  }

 public:
  virtual ~Instruction() = default;

  auto GetPrevious() const -> Instruction* {
    return previous_;
  }

  auto HasPrevious() const -> bool {
    return previous_ != nullptr;
  }

  auto GetNext() const -> Instruction* {
    return next_;
  }

  auto HasNext() const -> bool {
    return next_ != nullptr;
  }

  virtual auto IsDefinition() const -> bool {
    return false;
  }

  virtual auto ToString() const -> std::string = 0;
  virtual auto GetInstructionName() const -> std::string_view = 0;
  virtual auto Accept(InstructionVisitor* vis) -> bool = 0;

  virtual auto GetNumberOfInputs() const -> uint64_t {
    return 0;
  }

  virtual auto GetInputAt(const uint64_t idx) const -> Value* {
    return nullptr;
  }

  virtual auto HasInputAt(const uint64_t idx) const -> bool {
    return GetInputAt(idx) != nullptr;
  }

#define DEFINE_TYPE_CHECK(Name)             \
  virtual auto As##Name() -> Name##Instr* { \
    return nullptr;                         \
  }                                         \
  auto Is##Name() -> bool {                 \
    return As##Name() != nullptr;           \
  }
  FOR_EACH_INSTRUCTION(DEFINE_TYPE_CHECK)
#undef DEFINE_TYPE_CHECK
 public:
  static inline void Link(Instruction* lhs, Instruction* rhs) {
    lhs->SetNext(rhs);
    if (rhs)
      rhs->SetPrevious(lhs);
  }
};

#define HAS_NAMED_INPUT(Name, Pos)                  \
  static constexpr const auto k##Name##Pos = (Pos); \
                                                    \
 private:                                           \
  inline void Set##Name(Value* rhs) {               \
    return SetInputAt(k##Name##Pos, rhs);           \
  }                                                 \
                                                    \
 public:                                            \
  inline auto Get##Name() const -> Value* {         \
    return GetInputAt(k##Name##Pos);                \
  }                                                 \
  inline auto Has##Name() const -> bool {           \
    return Get##Name() != nullptr;                  \
  }

#define DECLARE_INSTRUCTION_TYPE(Name)                           \
 public:                                                         \
  auto ToString() const -> std::string override;                 \
  auto Accept(InstructionVisitor* vis) -> bool override;         \
  auto GetInstructionName() const -> std::string_view override { \
    return #Name;                                                \
  }                                                              \
  auto As##Name() -> Name##Instr* override {                     \
    return this;                                                 \
  }

class InstructionIterator {
 private:
  Instruction* current_;

 protected:
  explicit InstructionIterator(Instruction* current) :
    current_(current) {}

  inline auto current() const -> Instruction* {
    return current_;
  }

  inline void set_current(Instruction* instr) {
    current_ = instr;
  }

 public:
  virtual ~InstructionIterator() = default;

  virtual auto HasNext() const -> bool {
    return current() != nullptr;
  }

  virtual auto Next() -> Instruction* = 0;
};

class ForwardInstructionIterator : public InstructionIterator {
 public:
  explicit ForwardInstructionIterator(Instruction* current) :
    InstructionIterator(current) {}
  ~ForwardInstructionIterator() override = default;

  auto Next() -> Instruction* override {
    const auto next = current();
    set_current(next->GetNext());
    return next;
  }
};

template <const uint64_t NumberOfInputs>
class TemplateInstruction : public Instruction {
 private:
  Value* inputs_[NumberOfInputs];

 protected:
  TemplateInstruction() = default;

  void SetInputAt(const uint64_t idx, Value* value) override {
    inputs_[idx] = value;
  }

 public:
  ~TemplateInstruction() override = default;

  auto GetNumberOfInputs() const -> uint64_t override {
    return NumberOfInputs;
  }

  auto GetInputAt(const uint64_t idx) const -> Value* override {
    if (idx >= NumberOfInputs)
      return nullptr;
    return inputs_[idx];
  }

  auto HasInputAt(const uint64_t idx) const -> bool override {
    if (idx >= NumberOfInputs)
      return false;
    return inputs_[idx] != nullptr;
  }
};

using ValueList = std::vector<Value*>;

#define HAS_VALUE_LIST(Name, ListName)                     \
 private:                                                  \
  ValueList ListName{};                                    \
                                                           \
 protected:                                                \
  void Set##Name##At(const uint64_t idx, Value* rhs) {     \
    ListName.at(idx) = rhs;                                \
  }                                                        \
                                                           \
 public:                                                   \
  auto Get##Name##s() const -> const ValueList& {          \
    return ListName;                                       \
  }                                                        \
  inline auto Has##Name##s() const -> bool {               \
    return !ListName.empty();                              \
  }                                                        \
  auto GetNumberOf##Name##s() const -> uint64_t {          \
    return ListName.size();                                \
  }                                                        \
  auto Get##Name##At(const uint64_t idx) const -> Value* { \
    if (idx >= ListName.size())                            \
      return nullptr;                                      \
    return ListName[idx];                                  \
  }                                                        \
  auto Has##Name##At(const uint64_t idx) const -> bool {   \
    return Get##Name##At(idx) != nullptr;                  \
  }

#define HAS_INPUT_LIST                                           \
 private:                                                        \
  ValueList inputs_{};                                           \
                                                                 \
 protected:                                                      \
  void SetInputAt(const uint64_t idx, Value* rhs) override {     \
    inputs_.at(idx) = rhs;                                       \
  }                                                              \
                                                                 \
 public:                                                         \
  auto GetInputs() const -> const ValueList& {                   \
    return inputs_;                                              \
  }                                                              \
  inline auto HasInputs() const -> bool {                        \
    return !inputs_.empty();                                     \
  }                                                              \
  auto GetNumberOfInputs() const -> uint64_t override {          \
    return inputs_.size();                                       \
  }                                                              \
  auto GetInputAt(const uint64_t idx) const -> Value* override { \
    if (idx >= inputs_.size())                                   \
      return nullptr;                                            \
    return inputs_[idx];                                         \
  }                                                              \
  auto HasInputAt(const uint64_t idx) const -> bool override {   \
    return GetInputAt(idx) != nullptr;                           \
  }

class DynamicTemplateInstruction : public Instruction {
 protected:
  DynamicTemplateInstruction() = default;

 public:
  ~DynamicTemplateInstruction() override = default;

  HAS_INPUT_LIST;
};

class Definition : public Instruction {
 protected:
  Definition() = default;

 public:
  ~Definition() override = default;

  auto IsDefinition() const -> bool override {
    return true;
  }
};

template <const uint64_t NumberOfInputs>
class TemplateDefinition : public Definition {
 private:
  Value* inputs_[NumberOfInputs];

 protected:
  TemplateDefinition() = default;

  void SetInputAt(const uint64_t idx, Value* value) override {
    inputs_[idx] = value;
  }

 public:
  ~TemplateDefinition() override = default;

  auto GetNumberOfInputs() const -> uint64_t override {
    return NumberOfInputs;
  }

  auto GetInputAt(const uint64_t idx) const -> Value* override {
    if (idx >= NumberOfInputs)
      return nullptr;
    return inputs_[idx];
  }

  auto HasInputAt(const uint64_t idx) const -> bool override {
    if (idx >= NumberOfInputs)
      return false;
    return inputs_[idx] != nullptr;
  }
};

class DynamicTemplateDefinition : public Definition {
 protected:
  DynamicTemplateDefinition() = default;

 public:
  ~DynamicTemplateDefinition() override = default;

  HAS_INPUT_LIST;
};

class ConstantInstr : public Definition {
 private:
  Object* value_;

 public:
  explicit ConstantInstr(Object* value) :
    Definition(),
    value_(value) {}
  ~ConstantInstr() override = default;

  auto GetValue() const -> Object* {
    return value_;
  }

  DECLARE_INSTRUCTION_TYPE(Constant);

 public:
  static inline auto New(Object* value) -> ConstantInstr* {
    return new ConstantInstr(value);
  }
};

class UnaryOpInstr : public TemplateDefinition<1> {
 private:
  expr::UnaryOp op_;

 public:
  UnaryOpInstr(const expr::UnaryOp op, Value* value) :
    op_(op) {
    SetValue(value);
  }
  ~UnaryOpInstr() override = default;

  auto GetOp() const -> expr::UnaryOp {
    return op_;
  }

  HAS_NAMED_INPUT(Value, 0);
  DECLARE_INSTRUCTION_TYPE(UnaryOp);

 public:
  static inline auto New(const expr::UnaryOp op, Value* value) -> UnaryOpInstr* {
    return new UnaryOpInstr(op, value);
  }
};

class BinaryOpInstr : public TemplateDefinition<2> {
 private:
  expr::BinaryOp op_;

 public:
  BinaryOpInstr(const expr::BinaryOp op, Value* left, Value* right) :
    op_(op) {
    SetLeft(left);
    SetRight(right);
  }
  ~BinaryOpInstr() override = default;

  auto GetOp() const -> expr::BinaryOp {
    return op_;
  }

  HAS_NAMED_INPUT(Left, 0);
  HAS_NAMED_INPUT(Right, 1);
  DECLARE_INSTRUCTION_TYPE(BinaryOp);

 public:
  static inline auto New(const expr::BinaryOp op, Value* left, Value* right) -> BinaryOpInstr* {
    return new BinaryOpInstr(op, left, right);
  }

#define DEFINE_NEW(Name)                                                   \
  static inline auto New##Name(Value* lhs, Value* rhs) -> BinaryOpInstr* { \
    return New(expr::BinaryOp::k##Name, lhs, rhs);                         \
  }
  FOR_EACH_BINARY_OP(DEFINE_NEW)
#undef DEFINE_NEW
};

class AllocInstr : public Instruction {
 public:
  AllocInstr() = default;
  ~AllocInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(Alloc);

 public:
  static inline auto New() -> AllocInstr* {
    return new AllocInstr();
  }
};

class New : public Definition {
 protected:
  New() = default;

 public:
  ~New() override = default;
};

class NewRecordInstr : public New {
 public:
  NewRecordInstr() = default;
  ~NewRecordInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(NewRecord);

 public:
  static inline auto New() -> NewRecordInstr* {
    return new NewRecordInstr();
  }
};

class NewListInstr : public New {
 public:
  NewListInstr() = default;
  explicit NewListInstr(const ValueList inputs) :
    inputs_(std::move(inputs)) {}
  ~NewListInstr() override = default;

  HAS_INPUT_LIST;
  DECLARE_INSTRUCTION_TYPE(NewList);

 public:
  static inline auto New(const ValueList inputs) -> NewListInstr* {
    return new NewListInstr(std::move(inputs));
  }
};

class NewNodeInstr : public New {
 public:
  enum Kind {
#define DEFINE_KIND(Name) k##Name,
    FOR_EACH_ELEMENT_NODE(DEFINE_KIND)
#undef DEFINE_KIND
  };
 private:
  Kind kind_;

 public:
  explicit NewNodeInstr(const Kind kind) :
    kind_(kind) {}
  ~NewNodeInstr() override = default;

  auto GetKind() const -> Kind {
    return kind_;
  }

#define DEFINE_TYPE_CHECK(Name)          \
  inline auto Is##Name() const -> bool { \
    return GetKind() == Kind::k##Name;   \
  }
  FOR_EACH_ELEMENT_NODE(DEFINE_TYPE_CHECK)
#undef DEFINE_TYPE_CHECK

  DECLARE_INSTRUCTION_TYPE(NewNode);

 public:
  static inline auto New(const Kind kind) -> NewNodeInstr* {
    return new NewNodeInstr(kind);
  }

#define DEFINE_NEW(Name)                            \
  static inline auto New##Name() -> NewNodeInstr* { \
    return New(Kind::k##Name);                      \
  }
  FOR_EACH_ELEMENT_NODE(DEFINE_NEW)
#undef DEFINE_NEW
};

class SpreadInstr : public Instruction {
 public:
  SpreadInstr() = default;
  ~SpreadInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(Spread);

 public:
  static inline auto New() -> SpreadInstr* {
    return new SpreadInstr();
  }
};

class LoadIndexInstr : public Definition {
 private:
  uint64_t index_;

 public:
  explicit LoadIndexInstr(const uint64_t index) :
    index_(index) {}
  ~LoadIndexInstr() override = default;

  auto GetIndex() const -> uint64_t {
    return index_;
  }

  DECLARE_INSTRUCTION_TYPE(LoadIndex);

 public:
  static inline auto New(const uint64_t index) -> LoadIndexInstr* {
    return new LoadIndexInstr(index);
  }
};

class StoreIndexInstr : public TemplateInstruction<1> {
 private:
  uint64_t index_;

 public:
  explicit StoreIndexInstr(const uint64_t index, Value* value) :
    index_(index) {
    SetValue(value);
  }
  ~StoreIndexInstr() override = default;

  HAS_NAMED_INPUT(Value, 0);
  DECLARE_INSTRUCTION_TYPE(StoreIndex);

 public:
  static inline auto New(uint64_t index, Value* value) -> StoreIndexInstr* {
    return new StoreIndexInstr(index, value);
  }
};

class LoadPropertyInstr : public TemplateDefinition<1> {
 private:
  Property* property_;

  inline void SetProperty(Property* property) {
    property_ = property;
  }

 public:
  explicit LoadPropertyInstr(Value* instance, Property* property) {
    SetInstance(instance);
    SetProperty(property);
  }
  ~LoadPropertyInstr() override = default;

  HAS_NAMED_INPUT(Instance, 0);
  DECLARE_INSTRUCTION_TYPE(LoadProperty);

 public:
  static inline auto New(Value* instance, Property* property) -> LoadPropertyInstr* {
    return new LoadPropertyInstr(instance, property);
  }
};

class LoadPropertySafelyInstr : public TemplateDefinition<1> {
 private:
  Property* property_;

  inline void SetProperty(Property* property) {
    property_ = property;
  }

 public:
  explicit LoadPropertySafelyInstr(Value* instance, Property* property) {
    SetInstance(instance);
    SetProperty(property);
  }
  ~LoadPropertySafelyInstr() override = default;

  auto GetProperty() const -> Property* {
    return property_;
  }

  HAS_NAMED_INPUT(Instance, 0);
  DECLARE_INSTRUCTION_TYPE(LoadPropertySafely);

 public:
  static inline auto New(Value* instance, Property* property) -> LoadPropertySafelyInstr* {
    return new LoadPropertySafelyInstr(instance, property);
  }
};

class StorePropertyInstr : public TemplateInstruction<3> {
 private:
  Property* property_;

  inline void SetProperty(Property* p) {
    property_ = p;
  }

 public:
  StorePropertyInstr(Value* instance, Property* property, Value* value) {
    SetInstance(instance);
    SetProperty(property);
    SetValue(value);
  }
  ~StorePropertyInstr() override = default;

  auto GetProperty() const -> Property* {
    return property_;
  }

  HAS_NAMED_INPUT(Instance, 0);
  HAS_NAMED_INPUT(Value, 1);
  DECLARE_INSTRUCTION_TYPE(StoreProperty);

 public:
  static inline auto New(Value* instance, Property* property, Value* value) -> StorePropertyInstr* {
    return new StorePropertyInstr(instance, property, value);
  }
};

class CallInstr : public Definition {
 private:
  Value* target_;

 public:
  explicit CallInstr(Value* target) :
    target_(target) {}
  CallInstr(Value* target, const ValueList args) :
    target_(target),
    args_(std::move(args)) {}
  ~CallInstr() override = default;

  auto GetTarget() const -> Value* {
    return target_;
  }

  HAS_VALUE_LIST(Arg, args_);
  DECLARE_INSTRUCTION_TYPE(Call);

 public:
  static inline auto New(Value* target) -> CallInstr* {
    return new CallInstr(target);
  }

  static inline auto New(Value* target, const ValueList args) -> CallInstr* {
    return new CallInstr(target, std::move(args));
  }
};

class PipeCallInstr : public Definition {
 public:
  PipeCallInstr() = default;
  ~PipeCallInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(PipeCall);

 public:
  static inline auto New() -> PipeCallInstr* {
    return new PipeCallInstr();
  }
};

class ReturnInstr : public TemplateDefinition<1> {
 public:
  explicit ReturnInstr(Value* value) {
    SetValue(value);
  }
  ~ReturnInstr() override = default;

  HAS_NAMED_INPUT(Value, 0);
  DECLARE_INSTRUCTION_TYPE(Return);

 public:
  static inline auto New(Value* value) -> ReturnInstr* {
    return new ReturnInstr(value);
  }
};

class PhiInstr : public Definition {
 public:
  struct Incoming {
    TargetEntryInstr* predecessor;
    Value* value;
  };

  using IncomingList = std::vector<Incoming>;

 private:
  JoinEntryInstr* join_;
  IncomingList incoming_{};

  inline void AddIncoming(TargetEntryInstr* entry, Value* value) {
    incoming_.push_back({entry, value});
  }

 public:
  explicit PhiInstr(JoinEntryInstr* join) :
    Definition(),
    join_(join) {}
  ~PhiInstr() override = default;

  auto GetJoin() const -> JoinEntryInstr* {
    return join_;
  }

  auto GetIncoming() const -> const IncomingList& {
    return incoming_;
  }

  auto GetNumberOfIncoming() const -> uint64_t {
    return incoming_.size();
  }

  auto GetIncomingAt(const uint64_t idx) const -> const Incoming& {
    return incoming_[idx];
  }

  DECLARE_INSTRUCTION_TYPE(Phi);

 public:
  static inline auto New(JoinEntryInstr* join) -> PhiInstr* {
    return new PhiInstr(join);
  }
};

class GotoInstr : public Instruction {
 private:
  EntryInstr* target_;

 public:
  explicit GotoInstr(EntryInstr* target) :
    target_(target) {}
  ~GotoInstr() override = default;

  auto GetTarget() const -> EntryInstr* {
    return target_;
  }

  DECLARE_INSTRUCTION_TYPE(Goto);

 public:
  static inline auto New(EntryInstr* target) -> GotoInstr* {
    return new GotoInstr(target);
  }
};

class BranchInstr : public TemplateInstruction<1> {
 private:
  TargetEntryInstr* then_;
  TargetEntryInstr* else_;
  JoinEntryInstr* join_;

 public:
  BranchInstr(Value* condition, TargetEntryInstr* then_target, TargetEntryInstr* else_target, JoinEntryInstr* join) :
    then_(then_target),
    else_(else_target),
    join_(join) {
    SetCondition(condition);
  }
  ~BranchInstr() override = default;

  auto GetThen() const -> TargetEntryInstr* {
    return then_;
  }

  auto GetElse() const -> TargetEntryInstr* {
    return else_;
  }

  inline auto HasElse() const -> bool {
    return GetElse() != nullptr;
  }

  auto GetJoin() const -> JoinEntryInstr* {
    return join_;
  }

  HAS_NAMED_INPUT(Condition, 0);
  DECLARE_INSTRUCTION_TYPE(Branch);

 public:
  static inline auto New(Value* condition, TargetEntryInstr* then_target, TargetEntryInstr* else_target,
                         JoinEntryInstr* join) -> BranchInstr* {
    return new BranchInstr(condition, then_target, else_target, join);
  }
};

class LocalVariable;
class StoreLocalInstr : public TemplateInstruction<1> {
 private:
  LocalVariable* local_;

  inline void SetLocal(LocalVariable* local) {
    local_ = local;
  }

 public:
  explicit StoreLocalInstr(LocalVariable* local, Value* value) :
    TemplateInstruction<1>() {
    SetLocal(local);
    SetValue(value);
  }
  ~StoreLocalInstr() override = default;

  auto GetLocal() const -> LocalVariable* {
    return local_;
  }

  HAS_NAMED_INPUT(Value, 0);
  DECLARE_INSTRUCTION_TYPE(StoreLocal);

 public:
  static inline auto New(LocalVariable* local, Value* value) -> StoreLocalInstr* {
    return new StoreLocalInstr(local, value);
  }
};

class LoadLocalInstr : public Definition {
 private:
  LocalVariable* local_;

 public:
  explicit LoadLocalInstr(LocalVariable* local) :
    Definition(),
    local_(local) {}
  ~LoadLocalInstr() override = default;

  auto GetLocal() const -> LocalVariable* {
    return local_;
  }

  DECLARE_INSTRUCTION_TYPE(LoadLocal);

 public:
  static inline auto New(LocalVariable* local) -> LoadLocalInstr* {
    return new LoadLocalInstr(local);
  }
};

class ParameterInstr : public Definition {
 public:
  ParameterInstr() = default;
  ~ParameterInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(Parameter);

 public:
  static inline auto New() -> ParameterInstr* {
    return new ParameterInstr();
  }
};

class LoadFunctionInstr : public Definition {
 public:
  LoadFunctionInstr() = default;
  ~LoadFunctionInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(LoadFunction);

 public:
  static inline auto New() -> LoadFunctionInstr* {
    return new LoadFunctionInstr();
  }
};

using BlockId = uint64_t;

class EntryInstr : public Instruction {
  friend class FlowGraphBuilder;

 private:
  BlockId block_id_ = 0;

 protected:
  EntryInstr() = default;

  virtual void SetSuccessorAt(const uint64_t idx, EntryInstr* instr) {
    // do nothing
  }

  virtual void AddSuccessor(EntryInstr* instr) {
    // do nothing
  }

  virtual void SetPredecessorAt(const uint64_t idx, EntryInstr* instr) {
    // do nothing
  }

  virtual void AddPredecessor(EntryInstr* instr) {
    // do nothing
  }

  void SetBlockId(const BlockId rhs) {
    block_id_ = rhs;
  }

 public:
  ~EntryInstr() override = default;

  auto GetBlockId() const -> BlockId {
    return block_id_;
  }

  virtual auto GetNumberOfSuccessors() const -> uint64_t {
    return 0;
  }

  virtual auto GetSuccessorAt(const uint64_t idx) const -> EntryInstr* {
    return nullptr;
  }

  inline auto HasSuccessorAt(const uint64_t idx) const -> bool {
    return GetSuccessorAt(idx) != nullptr;
  }

  virtual auto GetNumberOfPredecessors() const -> uint64_t {
    return 0;
  }

  virtual auto GetPredecessorAt(const uint64_t idx) const -> EntryInstr* {
    return nullptr;
  }

  auto HasPredecessorAt(const uint64_t idx) const -> bool {
    return GetPredecessorAt(idx) != nullptr;
  }

  virtual auto GetLastInstruction() -> Instruction*;

  void Append(Instruction* join) {
    Instruction* last = this;
    while (last->HasNext())
      last = last->GetNext();
    Instruction::Link(last, join);
  }
};

#define _HAS_ENTRY_LIST(Name, List)                                      \
 private:                                                                \
  std::vector<EntryInstr*> List{};                                       \
                                                                         \
 protected:                                                              \
  void Set##Name##At(const uint64_t idx, EntryInstr* instr) override {   \
    List[idx] = instr;                                                   \
  }                                                                      \
  void Add##Name(EntryInstr* instr) override {                           \
    List.push_back(instr);                                               \
  }                                                                      \
                                                                         \
 public:                                                                 \
  auto GetNumberOf##Name##s() const -> uint64_t override {               \
    return List.size();                                                  \
  }                                                                      \
  auto Get##Name##At(const uint64_t idx) const -> EntryInstr* override { \
    return List[idx];                                                    \
  }

#define _HAS_SUCCESSOR_LIST(List)   _HAS_ENTRY_LIST(Successor, List)
#define HAS_SUCCESSOR_LIST          _HAS_SUCCESSOR_LIST(successors_)

#define _HAS_PREDECESSOR_LIST(List) _HAS_ENTRY_LIST(Predecessor, List)
#define HAS_PREDECESSOR_LIST        _HAS_PREDECESSOR_LIST(predecessors_)

class GraphEntryInstr : public EntryInstr {
 private:
  TargetEntryInstr* target_;

 public:
  explicit GraphEntryInstr(TargetEntryInstr* target) :
    EntryInstr(),
    target_(target) {}
  ~GraphEntryInstr() override = default;

  auto GetTarget() const -> TargetEntryInstr* {
    return target_;
  }

  inline auto HasTarget() const -> bool {
    return GetTarget() != nullptr;
  }

  auto GetLastInstruction() -> Instruction* override;

  HAS_SUCCESSOR_LIST;
  DECLARE_INSTRUCTION_TYPE(GraphEntry);

 public:
  static inline auto New(TargetEntryInstr* target) -> GraphEntryInstr* {
    return new GraphEntryInstr(target);
  }
};

using PhiList = std::vector<PhiInstr*>;

class JoinEntryInstr : public EntryInstr {
  friend class EffectVisitor;

 private:
  PhiList phis_{};

  inline void AddPhis(const PhiList phis) {
    phis_.insert(std::end(phis_), std::begin(phis), std::end(phis));
  }

  inline auto AddPhi(PhiInstr* phi) -> PhiInstr* {
    phis_.push_back(phi);
    return phi;
  }

 public:
  JoinEntryInstr() = default;
  explicit JoinEntryInstr(const PhiList phis) :
    phis_(std::move(phis)) {}
  ~JoinEntryInstr() override = default;

  auto GetPhis() const -> const PhiList& {
    return phis_;
  }

  auto GetNumberOfPhis() const -> uint64_t {
    return phis_.size();
  }

  auto GetPhiAt(const uint64_t idx) const -> PhiInstr* {
    return phis_.at(idx);
  }

  inline auto HasPhiAt(const uint64_t idx) const -> bool {
    return idx < phis_.size() && GetPhiAt(idx) != nullptr;
  }

  auto NewPhi() -> PhiInstr* {
    return AddPhi(PhiInstr::New(this));
  }

  DECLARE_INSTRUCTION_TYPE(JoinEntry);

 public:
  static inline auto New() -> JoinEntryInstr* {
    return new JoinEntryInstr();
  }

  static inline auto New(const PhiList phis) -> JoinEntryInstr* {
    return new JoinEntryInstr(std::move(phis));
  }
};

class TargetEntryInstr : public EntryInstr {
 public:
  TargetEntryInstr() = default;
  ~TargetEntryInstr() override = default;

  inline void AppendGoto(JoinEntryInstr* join) {
    return Append(GotoInstr::New(join));
  }

  HAS_SUCCESSOR_LIST;
  HAS_PREDECESSOR_LIST;
  DECLARE_INSTRUCTION_TYPE(TargetEntry);

 public:
  static inline auto New() -> TargetEntryInstr* {
    return new TargetEntryInstr();
  }
};

#undef HAS_INPUT_LIST
#undef HAS_NAMED_INPUT
#undef HAS_SUCCESSOR_LIST
#undef _HAS_SUCCESSOR_LIST
#undef HAS_PREDECESSOR_LIST
#undef _HAS_PREDECESSOR_LIST
#undef _HAS_ENTRY_LIST
}  // namespace kura

#endif  // KURA_IR_H
