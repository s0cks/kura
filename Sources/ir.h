#ifndef KURA_IR_H
#define KURA_IR_H

#include <functional>
#include <string>

#include "element.h"
#include "expr.h"
#include "object.h"

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
  Instruction* previous_;
  Instruction* next_;

 protected:
  Instruction() = default;

  void SetPrevious(Instruction* p) {
    previous_ = p;
  }

  void SetNext(Instruction* n) {
    next_ = n;
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

  void SetInputAt(const uint64_t idx, Value* value) {
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

  void SetInputAt(const uint64_t idx, Value* value) {
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
  static constexpr const auto kValuePos = 0;

 private:
  expr::UnaryOp op_;

  inline void SetValue(Value* value) {
    return SetInputAt(kValuePos, value);
  }

 public:
  UnaryOpInstr(const expr::UnaryOp op, Value* value) :
    TemplateDefinition<1>(),
    op_(op) {
    SetValue(value);
  }
  ~UnaryOpInstr() override = default;

  auto GetOp() const -> expr::UnaryOp {
    return op_;
  }

  inline auto GetValue() const -> Value* {
    return GetInputAt(kValuePos);
  }

  DECLARE_INSTRUCTION_TYPE(UnaryOp);

 public:
  static inline auto New(const expr::UnaryOp op, Value* value) -> UnaryOpInstr* {
    return new UnaryOpInstr(op, value);
  }
};

class BinaryOpInstr : public TemplateDefinition<2> {
  static constexpr const auto kLeftPos = 0;
  static constexpr const auto kRightPos = 1;

 private:
  expr::BinaryOp op_;

  inline void SetLeft(Value* value) {
    SetInputAt(kLeftPos, value);
  }

  inline void SetRight(Value* value) {
    SetInputAt(kRightPos, value);
  }

 public:
  BinaryOpInstr(const expr::BinaryOp op, Value* left, Value* right) :
    TemplateDefinition<2>(),
    op_(op) {
    SetLeft(left);
    SetRight(right);
  }
  ~BinaryOpInstr() override = default;

  auto GetOp() const -> expr::BinaryOp {
    return op_;
  }

  inline auto GetLeft() const -> Value* {
    return GetInputAt(kLeftPos);
  }

  inline auto GetRight() const -> Value* {
    return GetInputAt(kRightPos);
  }

  DECLARE_INSTRUCTION_TYPE(BinaryOp);

 public:
  static inline auto New(const expr::BinaryOp op, Value* left, Value* right) -> BinaryOpInstr* {
    return new BinaryOpInstr(op, left, right);
  }
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

using ValueList = std::vector<Value*>;

class NewListInstr : public New {
 private:
  ValueList values_{};

 public:
  explicit NewListInstr(const ValueList values) :
    values_(std::move(values)) {}
  ~NewListInstr() override = default;

  auto GetValues() const -> const ValueList& {
    return values_;
  }

  DECLARE_INSTRUCTION_TYPE(NewList);

 public:
  static inline auto New(const ValueList values) -> NewListInstr* {
    return new NewListInstr(std::move(values));
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
 public:
  LoadIndexInstr() = default;
  ~LoadIndexInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(LoadIndex);

 public:
  static inline auto New() -> LoadIndexInstr* {
    return new LoadIndexInstr();
  }
};

class StoreIndexInstr : public Instruction {
 public:
  StoreIndexInstr() = default;
  ~StoreIndexInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(StoreIndex);

 public:
  static inline auto New() -> StoreIndexInstr* {
    return new StoreIndexInstr();
  }
};

class LoadPropertyInstr : public Definition {
 private:
  Property* property_;

  inline void SetProperty(Property* property) {
    property_ = property;
  }

 public:
  explicit LoadPropertyInstr(Property* property) :
    Definition() {
    SetProperty(property);
  }
  ~LoadPropertyInstr() override = default;

  auto GetProperty() const -> Property* {
    return property_;
  }

  DECLARE_INSTRUCTION_TYPE(LoadProperty);

 public:
  static inline auto New(Property* property) -> LoadPropertyInstr* {
    return new LoadPropertyInstr(property);
  }
};

class LoadPropertySafelyInstr : public Definition {
 private:
  Property* property_;

  inline void SetProperty(Property* property) {
    property_ = property;
  }

 public:
  explicit LoadPropertySafelyInstr(Property* property) :
    Definition() {
    SetProperty(property);
  }
  ~LoadPropertySafelyInstr() override = default;

  auto GetProperty() const -> Property* {
    return property_;
  }

  DECLARE_INSTRUCTION_TYPE(LoadPropertySafely);

 public:
  static inline auto New(Property* property) -> LoadPropertySafelyInstr* {
    return new LoadPropertySafelyInstr(property);
  }
};

class StorePropertyInstr : public TemplateInstruction<2> {
  static constexpr const auto kRecordPos = 0;
  static constexpr const auto kValuePos = 1;

 private:
  Property* property_;

  inline void SetProperty(Property* p) {
    property_ = p;
  }

  inline void SetRecord(Value* value) {
    return SetInputAt(kRecordPos, value);
  }

  inline void SetValue(Value* value) {
    return SetInputAt(kValuePos, value);
  }

 public:
  StorePropertyInstr(Value* record, Property* property, Value* value) :
    TemplateInstruction() {
    SetRecord(record);
    SetProperty(property);
    SetValue(value);
  }
  ~StorePropertyInstr() override = default;

  inline auto GetRecord() const -> Value* {
    return GetInputAt(kRecordPos);
  }

  auto GetProperty() const -> Property* {
    return property_;
  }

  inline auto GetValue() const -> Value* {
    return GetInputAt(kValuePos);
  }

  inline auto HasValue() const -> bool {
    return GetValue() != nullptr;
  }

  DECLARE_INSTRUCTION_TYPE(StoreProperty);

 public:
  static inline auto New(Value* record, Property* property, Value* value) -> StorePropertyInstr* {
    return new StorePropertyInstr(record, property, value);
  }
};

class CallInstr : public Definition {
 private:
  Value* target_;
  ValueList args_{};

 public:
  CallInstr(Value* target, const ValueList args) :
    Definition(),
    target_(target),
    args_(std::move(args)) {}
  ~CallInstr() override = default;

  auto GetTarget() const -> Value* {
    return target_;
  }

  auto GetArgs() const -> const ValueList& {
    return args_;
  }

  inline auto HasArgs() const -> bool {
    return !args_.empty();
  }

  auto GetNumberOfArgs() const -> uint64_t {
    return args_.size();
  }

  auto GetArgAt(const uint64_t idx) const -> Value* {
    return args_[idx];
  }

  inline auto HasArgAt(const uint64_t idx) const -> bool {
    return GetArgAt(idx) != nullptr;
  }

  DECLARE_INSTRUCTION_TYPE(Call);

 public:
  static inline auto New(Value* target, const ValueList args = {}) -> CallInstr* {
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
  static constexpr const auto kValuePos = 0;

 private:
  inline void SetValue(Value* value) {
    return SetInputAt(kValuePos, value);
  }

 public:
  explicit ReturnInstr(Value* value) :
    TemplateDefinition<1>() {
    SetValue(value);
  }
  ~ReturnInstr() override = default;

  auto GetValue() const -> Value* {
    return GetInputAt(kValuePos);
  }

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
  EntryInstr* entry_;

 public:
  explicit GotoInstr(EntryInstr* entry) :
    entry_(entry) {}
  ~GotoInstr() override = default;

  auto GetEntry() const -> EntryInstr* {
    return entry_;
  }

  DECLARE_INSTRUCTION_TYPE(Goto);

 public:
  static inline auto New(EntryInstr* entry) -> GotoInstr* {
    return new GotoInstr(entry);
  }
};

class BranchInstr : public Instruction {
 private:
  TargetEntryInstr* then_;
  TargetEntryInstr* else_;
  JoinEntryInstr* join_;

 public:
  BranchInstr(TargetEntryInstr* then_target, TargetEntryInstr* else_target, JoinEntryInstr* join) :
    Instruction(),
    then_(then_target),
    else_(else_target),
    join_(join) {}
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

  DECLARE_INSTRUCTION_TYPE(Branch);

 public:
  static inline auto New(TargetEntryInstr* then_target, TargetEntryInstr* else_target, JoinEntryInstr* join)
      -> BranchInstr* {
    return new BranchInstr(then_target, else_target, join);
  }
};

class LocalVariable;
class StoreLocalInstr : public TemplateInstruction<1> {
  static constexpr const auto kValuePos = 0;

 private:
  LocalVariable* local_;

  inline void SetLocal(LocalVariable* local) {
    local_ = local;
  }

  inline void SetValue(Value* value) {
    return SetInputAt(kValuePos, value);
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

  inline auto GetValue() const -> Value* {
    return GetInputAt(kValuePos);
  }

  inline auto HasValue() const -> bool {
    return GetValue() != nullptr;
  }

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

class EntryInstr : public Instruction {
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

 public:
  ~EntryInstr() override = default;

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

class GraphEntryInstr : public EntryInstr {
 private:
  TargetEntryInstr* target_;
  std::vector<EntryInstr*> successors_{};

 protected:
  void SetPredecessorAt(const uint64_t idx, EntryInstr* instr) override {
    successors_[idx] = instr;
  }

  void AddPredecessor(EntryInstr* instr) override {
    successors_.push_back(instr);
  }

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

  auto GetNumberOfSuccessors() const -> uint64_t override {
    return successors_.size();
  }

  auto GetSuccessorAt(const uint64_t idx) const -> EntryInstr* override {
    return successors_[idx];
  }

  auto GetLastInstruction() -> Instruction* override;
  DECLARE_INSTRUCTION_TYPE(GraphEntry);

 public:
  static inline auto New(TargetEntryInstr* target) -> GraphEntryInstr* {
    return new GraphEntryInstr(target);
  }
};

class JoinEntryInstr : public EntryInstr {
 private:
  std::vector<PhiInstr*> phis_{};

  inline auto AddPhi(PhiInstr* phi) -> PhiInstr* {
    phis_.push_back(phi);
    return phi;
  }

 public:
  JoinEntryInstr() = default;
  ~JoinEntryInstr() override = default;

  auto GetPhis() const -> const std::vector<PhiInstr*>& {
    return phis_;
  }

  auto GetNumberOfPhis() const -> uint64_t {
    return phis_.size();
  }

  auto GetPhiAt(const uint64_t idx) const -> PhiInstr* {
    return phis_[idx];
  }

  auto NewPhi() -> PhiInstr* {
    return AddPhi(PhiInstr::New(this));
  }

  DECLARE_INSTRUCTION_TYPE(JoinEntry);

 public:
  static inline auto New() -> JoinEntryInstr* {
    return new JoinEntryInstr();
  }
};

class TargetEntryInstr : public EntryInstr {
 private:
  std::vector<EntryInstr*> predecessors_{};
  std::vector<EntryInstr*> successors_{};

 protected:
  void SetPredecessorAt(const uint64_t idx, EntryInstr* instr) override {
    predecessors_[idx] = instr;
  }

  void AddPredecessor(EntryInstr* instr) override {
    predecessors_.push_back(instr);
  }

  void SetSuccessorAt(const uint64_t idx, EntryInstr* instr) override {
    successors_[idx] = instr;
  }

  void AddSuccessor(EntryInstr* instr) override {
    successors_.push_back(instr);
  }

 public:
  TargetEntryInstr() = default;
  ~TargetEntryInstr() override = default;

  auto GetNumberOfPredecessors() const -> uint64_t override {
    return predecessors_.size();
  }

  auto GetPredecessorAt(const uint64_t idx) const -> EntryInstr* override {
    return predecessors_[idx];
  }

  auto GetNumberOfSuccessors() const -> uint64_t override {
    return successors_.size();
  }

  auto GetSuccessorAt(const uint64_t idx) const -> EntryInstr* override {
    return successors_[idx];
  }

  void AppendGoto(JoinEntryInstr* join) {
    return Append(GotoInstr::New(join));
  }

  DECLARE_INSTRUCTION_TYPE(TargetEntry);

 public:
  static inline auto New() -> TargetEntryInstr* {
    return new TargetEntryInstr();
  }
};
}  // namespace kura

#endif  // KURA_IR_H
