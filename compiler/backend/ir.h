#ifndef KURA_IR_H
#define KURA_IR_H

#include <functional>
#include <string>

#include "binary_op.h"
#include "color.h"
#include "dom/kind.h"
#include "frontend/expr.h"
#include "frontend/math_op.h"
#include "object.h"
#include "property.h"
#include "record.h"
#include "type_id.h"
#include "unary_op.h"

namespace kura {
#define FOR_EACH_INSTRUCTION(V) \
  V(GraphEntry)                 \
  V(TargetEntry)                \
  V(JoinEntry)                  \
  V(Panic)                      \
  V(Constant)                   \
  V(UnaryOp)                    \
  V(BinaryOp)                   \
  V(NewTuple)                   \
  V(NewList)                    \
  V(NewRecord)                  \
  V(NewNode)                    \
  V(Spread)                     \
  V(StoreIndex)                 \
  V(LoadIndex)                  \
  V(SeqAppend)                  \
  V(SliceTuple)                 \
  V(StoreProperty)              \
  V(LoadProperty)               \
  V(LoadPropertySafely)         \
  V(LoadFunction)               \
  V(LoadLength)                 \
  V(NewVariant)                 \
  V(TestVariant)                \
  V(LoadVariantPayload)         \
  V(NewClosure)                 \
  V(Parameter)                  \
  V(Call)                       \
  V(Cast)                       \
  V(PipeCall)                   \
  V(MathCall)                   \
  V(Return)                     \
  V(Goto)                       \
  V(Branch)                     \
  V(Phi)                        \
  V(Label)                      \
  V(StoreLocal)                 \
  V(LoadLocal)                  \
  V(Alloc)                      \
  V(AllocImage)                 \
  V(RectIntersects)             \
  V(CheckRendererStatus)        \
  V(BeginFrame)                 \
  V(EndFrame)                   \
  V(MeasureText)                \
  V(LayoutSetText)              \
  V(LayoutSetProperty)          \
  V(LayoutRun)                  \
  V(LayoutSlotBegin)            \
  V(LayoutSlotInstance)         \
  V(LayoutSlotEnd)              \
  V(LayoutRect)                 \
  V(FillRect)                   \
  V(PaintBox)                   \
  V(PushClip)                   \
  V(PopClip)                    \
  V(FillRoundRect)              \
  V(FillCircle)                 \
  V(FillPolygon)                \
  V(FillBezier)                 \
  V(DrawLine)                   \
  V(DrawRect)                   \
  V(DrawRoundRect)              \
  V(DrawCircle)                 \
  V(DrawEllipse)                \
  V(DrawPolygon)                \
  V(DrawText)                   \
  V(DrawInput)                  \
  V(DrawImage)                  \
  V(DrawStrokeBezier)           \
  V(DrawQuadBezier)             \
  V(RegisterEvent)              \
  V(TracyBeginZone)             \
  V(TracyEndZone)               \
  V(RectPastClip)               \
  V(UpdateRecord)               \
  V(Retain)                     \
  V(Release)                    \
  V(NewColor)                   \
  V(CastColor)                  \
  V(Assert)

class Instruction;
class EntryInstr;
class FlowGraph;
class CompileType;
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
 public:
  using InstructionId = uint64_t;

 private:
  static inline auto NextId() -> InstructionId {
    static InstructionId next = 0;
    return ++next;
  }

  InstructionId id_ = NextId();
  Instruction* previous_ = nullptr;
  Instruction* next_ = nullptr;

 protected:
  Instruction() = default;

 public:
  virtual ~Instruction() = default;

  // Unique and stable: the SSA name (`%id`) of a definition in printed IR.
  auto GetId() const -> InstructionId {
    return id_;
  }

  auto GetPrevious() const -> Instruction* {
    return previous_;
  }

  void SetPrevious(Instruction* p) {
    previous_ = p;
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

  void SetNext(Instruction* n) {
    next_ = n;
  }

  virtual auto IsDefinition() const -> bool {
    return false;
  }

  virtual auto IsGraphicsInstr() const -> bool {
    return false;
  }

  virtual auto ToString() const -> std::string = 0;
  virtual auto GetInstructionName() const -> std::string_view = 0;
  virtual auto Accept(InstructionVisitor* vis) -> bool = 0;

  virtual auto GetNumberOfInputs() const -> uint64_t {
    return 0;
  }

  virtual void SetInputAt(const uint64_t idx, Value* rhs) {
    // do nothing
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

#define HAS_NAMED_INPUT(Name, P)                  \
 public:                                          \
  static constexpr const auto k##Name##Pos = (P); \
                                                  \
 private:                                         \
  inline void Set##Name(Value* rhs) {             \
    return SetInputAt(k##Name##Pos, rhs);         \
  }                                               \
                                                  \
 public:                                          \
  inline auto Get##Name() const -> Value* {       \
    return GetInputAt(k##Name##Pos);              \
  }                                               \
  inline auto Has##Name() const -> bool {         \
    return Get##Name() != nullptr;                \
  }

#define DECLARE_INSTRUCTION_TYPE(Name)                           \
  friend class EffectVisitor;                                    \
  friend class ValueVisitor;                                     \
                                                                 \
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
  Value* inputs_[NumberOfInputs]{};

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
 private:
  CompileType* compile_type_ = nullptr;
  bool stack_allocated_ = false;

 protected:
  Definition() = default;

 public:
  ~Definition() override = default;

  auto GetCompileType() const -> CompileType* {
    return compile_type_;
  }

  inline auto HasCompileType() const -> bool {
    return compile_type_ != nullptr;
  }

  void SetCompileType(CompileType* type) {
    compile_type_ = type;
  }

  // Set by the escape analysis on an allocation that doesn't leave its function: it is a block of the function's
  // stack, not an object in a heap space.
  inline auto IsStackAllocated() const -> bool {
    return stack_allocated_;
  }

  inline void SetStackAllocated(const bool rhs = true) {
    stack_allocated_ = rhs;
  }

  auto IsDefinition() const -> bool override {
    return true;
  }
};

template <const uint64_t NumberOfInputs>
class TemplateDefinition : public Definition {
 private:
  Value* inputs_[NumberOfInputs]{};

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

#define DEFINE_TYPE_CHECK(Name)                                        \
  inline auto IsConstant##Name() const -> bool {                       \
    return value_ && value_->GetType() == BuiltinTypes::k##Name##Type; \
  }

  FOR_EACH_TYPE(DEFINE_TYPE_CHECK)
#undef DEFINE_TYPE_CHECK

#define DEFINE_TYPE_CAST(Name)                                         \
  inline auto AsConstant##Name() const -> Name* {                      \
    return IsConstant##Name() ? dynamic_cast<Name*>(value_) : nullptr; \
  }

  FOR_EACH_TYPE(DEFINE_TYPE_CAST)
#undef DEFINE_TYPE_CAST

  auto GetCanonicalKey() const -> std::string;

  DECLARE_INSTRUCTION_TYPE(Constant);

 public:
  static inline auto New(Object* value) -> ConstantInstr* {
    return new ConstantInstr(value);
  }
};

class UnaryOpInstr : public TemplateDefinition<1> {
 private:
  UnaryOp op_;

 public:
  UnaryOpInstr(const UnaryOp op, Value* value) :
    op_(op) {
    SetValue(value);
  }
  ~UnaryOpInstr() override = default;

  auto GetOp() const -> UnaryOp {
    return op_;
  }

  HAS_NAMED_INPUT(Value, 0);
  DECLARE_INSTRUCTION_TYPE(UnaryOp);

 public:
  static inline auto New(const UnaryOp op, Value* value) -> UnaryOpInstr* {
    return new UnaryOpInstr(op, value);
  }
};

class BinaryOpInstr : public TemplateDefinition<2> {
 private:
  BinaryOp op_;

 public:
  BinaryOpInstr(const BinaryOp op, Value* left, Value* right) :
    op_(op) {
    SetLeft(left);
    SetRight(right);
  }
  ~BinaryOpInstr() override = default;

  auto GetOp() const -> BinaryOp {
    return op_;
  }

  HAS_NAMED_INPUT(Left, 0);
  HAS_NAMED_INPUT(Right, 1);
  DECLARE_INSTRUCTION_TYPE(BinaryOp);

 public:
  static inline auto New(const BinaryOp op, Value* left, Value* right) -> BinaryOpInstr* {
    return new BinaryOpInstr(op, left, right);
  }

#define DEFINE_NEW(Name)                                                   \
  static inline auto New##Name(Value* lhs, Value* rhs) -> BinaryOpInstr* { \
    return New(BinaryOp::k##Name, lhs, rhs);                               \
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

// A record, with the room it will need: a heap record is as big as it is made, so the number of entries it is given
// (`properties`) and the records that are spread into it (the inputs, which are read only for how many fields they
// have) are here, and the StoreProperty and Spread instructions that fill it follow.
class NewRecordInstr : public New {
 private:
  uint64_t properties_ = 0;

 public:
  NewRecordInstr() = default;
  explicit NewRecordInstr(const uint64_t properties, const ValueList spreads = {}) :
    properties_(properties),
    inputs_(std::move(spreads)) {}
  ~NewRecordInstr() override = default;

  inline auto GetProperties() const -> uint64_t {
    return properties_;
  }

  HAS_INPUT_LIST;
  DECLARE_INSTRUCTION_TYPE(NewRecord);

 public:
  static inline auto New(const uint64_t properties = 0, const ValueList spreads = {}) -> NewRecordInstr* {
    return new NewRecordInstr(properties, std::move(spreads));
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
#define DEFINE_KIND(Name, Tag) k##Name,
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

#define DEFINE_TYPE_CHECK(Name, Tag)     \
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

#define DEFINE_NEW(Name, Tag)                       \
  static inline auto New##Name() -> NewNodeInstr* { \
    return New(Kind::k##Name);                      \
  }
  FOR_EACH_ELEMENT_NODE(DEFINE_NEW)
#undef DEFINE_NEW
};

class SpreadInstr : public TemplateInstruction<2> {
 public:
  SpreadInstr(Value* instance, Value* value) {
    SetInstance(instance);
    SetValue(value);
  }
  ~SpreadInstr() override = default;

  HAS_NAMED_INPUT(Instance, 0);
  HAS_NAMED_INPUT(Value, 1);
  DECLARE_INSTRUCTION_TYPE(Spread);

 public:
  static inline auto New(Value* instance, Value* value) -> SpreadInstr* {
    return new SpreadInstr(instance, value);
  }
};

class LoadIndexInstr : public TemplateDefinition<2> {
 public:
  LoadIndexInstr(Value* instance, Value* index) {
    SetInstance(instance);
    SetIndex(index);
  }
  ~LoadIndexInstr() override = default;

  HAS_NAMED_INPUT(Instance, 0);
  HAS_NAMED_INPUT(Index, 1);
  DECLARE_INSTRUCTION_TYPE(LoadIndex);

 public:
  static inline auto New(Value* instance, Value* index) -> LoadIndexInstr* {
    return new LoadIndexInstr(instance, index);
  }
};

class StoreIndexInstr : public TemplateInstruction<3> {
 public:
  StoreIndexInstr(Value* instance, Value* index, Value* value) {
    SetInstance(instance);
    SetIndex(index);
    SetValue(value);
  }
  ~StoreIndexInstr() override = default;

  HAS_NAMED_INPUT(Instance, 0);
  HAS_NAMED_INPUT(Index, 1);
  HAS_NAMED_INPUT(Value, 2);
  DECLARE_INSTRUCTION_TYPE(StoreIndex);

 public:
  static inline auto New(Value* instance, Value* index, Value* value) -> StoreIndexInstr* {
    return new StoreIndexInstr(instance, index, value);
  }
};

class LoadLengthInstr : public TemplateDefinition<1> {
 public:
  explicit LoadLengthInstr(Value* instance) {
    SetInstance(instance);
  }
  ~LoadLengthInstr() override = default;

  HAS_NAMED_INPUT(Instance, 0);
  DECLARE_INSTRUCTION_TYPE(LoadLength);

 public:
  static inline auto New(Value* instance) -> LoadLengthInstr* {
    return new LoadLengthInstr(instance);
  }
};

class NewVariantInstr : public Definition {
 private:
  std::string name_;
  Value* payload_;
  // A variant that is written without its payload where something other than the program fills it in: the
  // message of `on-input` is `Typed`, and the runtime makes it `Typed("what was typed")`.
  bool open_ = false;

 public:
  NewVariantInstr(const std::string name, Value* payload) :
    Definition(),
    name_(std::move(name)),
    payload_(payload) {}
  ~NewVariantInstr() override = default;

  inline auto IsOpen() const -> bool {
    return open_;
  }

  inline void SetOpen(const bool rhs = true) {
    open_ = rhs;
  }

  auto GetVariantName() const -> const std::string& {
    return name_;
  }

  auto GetPayload() const -> Value* {
    return payload_;
  }

  inline auto HasPayload() const -> bool {
    return GetPayload() != nullptr;
  }

  auto GetNumberOfInputs() const -> uint64_t override {
    return HasPayload() ? 1 : 0;
  }

  auto GetInputAt(const uint64_t idx) const -> Value* override {
    return idx == 0 ? payload_ : nullptr;
  }

  void SetInputAt(const uint64_t idx, Value* rhs) override {
    if (idx == 0)
      payload_ = rhs;
  }

  DECLARE_INSTRUCTION_TYPE(NewVariant);

 public:
  static inline auto New(const std::string name, Value* payload = nullptr) -> NewVariantInstr* {
    return new NewVariantInstr(std::move(name), payload);
  }

  static inline auto NewOpen(const std::string name) -> NewVariantInstr* {
    const auto instr = new NewVariantInstr(std::move(name), nullptr);
    instr->SetOpen();
    return instr;
  }
};

class TestVariantInstr : public TemplateDefinition<1> {
 private:
  std::string name_;

 public:
  TestVariantInstr(Value* value, const std::string name) :
    name_(std::move(name)) {
    SetValue(value);
  }
  ~TestVariantInstr() override = default;

  auto GetVariantName() const -> const std::string& {
    return name_;
  }

  HAS_NAMED_INPUT(Value, 0);
  DECLARE_INSTRUCTION_TYPE(TestVariant);

 public:
  static inline auto New(Value* value, const std::string name) -> TestVariantInstr* {
    return new TestVariantInstr(value, std::move(name));
  }
};

class LoadVariantPayloadInstr : public TemplateDefinition<1> {
 private:
  std::string name_;

 public:
  LoadVariantPayloadInstr(Value* value, const std::string name) :
    name_(std::move(name)) {
    SetValue(value);
  }
  ~LoadVariantPayloadInstr() override = default;

  auto GetVariantName() const -> const std::string& {
    return name_;
  }

  HAS_NAMED_INPUT(Value, 0);
  DECLARE_INSTRUCTION_TYPE(LoadVariantPayload);

 public:
  static inline auto New(Value* value, const std::string name) -> LoadVariantPayloadInstr* {
    return new LoadVariantPayloadInstr(value, std::move(name));
  }
};

class NewClosureInstr : public Definition {
 private:
  FlowGraph* target_;

 public:
  NewClosureInstr(FlowGraph* target, const ValueList captures) :
    Definition(),
    target_(target),
    captures_(std::move(captures)) {}
  ~NewClosureInstr() override = default;

  auto GetTarget() const -> FlowGraph* {
    return target_;
  }

  HAS_VALUE_LIST(Capture, captures_);

  auto GetNumberOfInputs() const -> uint64_t override {
    return captures_.size();
  }

  auto GetInputAt(const uint64_t idx) const -> Value* override {
    return GetCaptureAt(idx);
  }

  void SetInputAt(const uint64_t idx, Value* rhs) override {
    return SetCaptureAt(idx, rhs);
  }

  DECLARE_INSTRUCTION_TYPE(NewClosure);

 public:
  static inline auto New(FlowGraph* target, const ValueList captures = {}) -> NewClosureInstr* {
    return new NewClosureInstr(target, std::move(captures));
  }
};

class LoadPropertyInstr : public TemplateDefinition<1> {
 private:
  Property* property_;
  bool move_ = false;

  inline void SetProperty(Property* property) {
    property_ = property;
  }

 public:
  explicit LoadPropertyInstr(Value* instance, Property* property) {
    SetInstance(instance);
    SetProperty(property);
  }
  ~LoadPropertyInstr() override = default;

  auto GetProperty() const -> Property* {
    return property_;
  }

  auto IsMove() const -> bool {
    return move_;
  }

  void SetMove(const bool rhs) {
    move_ = rhs;
  }

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

class StorePropertyInstr : public TemplateInstruction<2> {
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

class MathCallInstr : public Definition {
 private:
  MathOp op_;

 public:
  explicit MathCallInstr(MathOp op) :
    op_(op) {}
  MathCallInstr(MathOp op, const ValueList args) :
    op_(op),
    args_(std::move(args)) {}
  ~MathCallInstr() override = default;

  auto GetOp() const -> MathOp {
    return op_;
  }

  HAS_VALUE_LIST(Arg, args_);

  auto GetNumberOfInputs() const -> uint64_t override {
    return args_.size();
  }

  auto GetInputAt(const uint64_t idx) const -> Value* override {
    return GetArgAt(idx);
  }

  void SetInputAt(const uint64_t idx, Value* rhs) override {
    SetArgAt(idx, rhs);
  }

  DECLARE_INSTRUCTION_TYPE(MathCall);

 public:
  static inline auto New(MathOp op) -> MathCallInstr* {
    return new MathCallInstr(op);
  }

  static inline auto New(MathOp op, const ValueList args) -> MathCallInstr* {
    return new MathCallInstr(op, std::move(args));
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

  auto GetNumberOfInputs() const -> uint64_t override {
    return 1 + args_.size();
  }

  auto GetInputAt(const uint64_t idx) const -> Value* override {
    return idx == 0 ? target_ : GetArgAt(idx - 1);
  }

  void SetInputAt(const uint64_t idx, Value* rhs) override {
    if (idx == 0)
      target_ = rhs;
    else
      SetArgAt(idx - 1, rhs);
  }

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
    if (value)
      SetValue(value);
  }
  ~ReturnInstr() override = default;

  auto GetNumberOfInputs() const -> uint64_t override {
    return HasValue() ? 1 : 0;
  }

  HAS_NAMED_INPUT(Value, 0);
  DECLARE_INSTRUCTION_TYPE(Return);

 public:
  static inline auto New(Value* value = nullptr) -> ReturnInstr* {
    return new ReturnInstr(value);
  }
};

class PhiInstr : public Definition {
 public:
  struct Incoming {
    EntryInstr* predecessor;
    Value* value;

    auto ToString() const -> std::string;

    friend auto operator<<(std::ostream& stream, const Incoming& rhs) -> std::ostream& {
      return stream << rhs.ToString();
    }
  };

  using IncomingList = std::vector<Incoming>;

 private:
  JoinEntryInstr* join_;
  IncomingList incoming_{};

 public:
  inline void AddIncoming(EntryInstr* entry, Value* value) {
    incoming_.push_back({entry, value});
  }

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

  auto GetIncoming() -> IncomingList& {
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

  void SetThen(TargetEntryInstr* rhs) {
    then_ = rhs;
  }

  auto GetElse() const -> TargetEntryInstr* {
    return else_;
  }

  inline auto HasElse() const -> bool {
    return GetElse() != nullptr;
  }

  void SetElse(TargetEntryInstr* rhs) {
    else_ = rhs;
  }

  auto GetJoin() const -> JoinEntryInstr* {
    return join_;
  }

  void SetJoin(JoinEntryInstr* rhs) {
    join_ = rhs;
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
 private:
  LocalVariable* local_;
  uint64_t index_;
  bool capture_;
  bool borrowed_ = false;

 public:
  ParameterInstr(LocalVariable* local, const uint64_t index, const bool capture = false) :
    Definition(),
    local_(local),
    index_(index),
    capture_(capture) {}
  ~ParameterInstr() override = default;

  auto GetLocal() const -> LocalVariable* {
    return local_;
  }

  auto GetIndex() const -> uint64_t {
    return index_;
  }

  inline auto IsCapture() const -> bool {
    return capture_;
  }

  auto IsBorrowed() const -> bool {
    return borrowed_;
  }

  void SetBorrowed(const bool rhs) {
    borrowed_ = rhs;
  }

  DECLARE_INSTRUCTION_TYPE(Parameter);

 public:
  static inline auto New(LocalVariable* local, const uint64_t index, const bool capture = false) -> ParameterInstr* {
    return new ParameterInstr(local, index, capture);
  }
};

class LoadFunctionInstr : public Definition {
 private:
  Function* function_;

 public:
  explicit LoadFunctionInstr(Function* function) :
    Definition(),
    function_(function) {}
  ~LoadFunctionInstr() override = default;

  auto GetFunction() const -> Function* {
    return function_;
  }

  DECLARE_INSTRUCTION_TYPE(LoadFunction);

 public:
  static inline auto New(Function* function) -> LoadFunctionInstr* {
    return new LoadFunctionInstr(function);
  }
};

using BlockId = uint64_t;

class EntryInstr : public Instruction {
  friend class EffectVisitor;
  friend class ValueVisitor;
  friend class FlowGraphBuilder;
  friend class FlowGraph;

 private:
  BlockId block_id_ = 0;
  String* label_ = nullptr;

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

  void SetBlockId(const BlockId rhs) {
    block_id_ = rhs;
  }

  void SetLabel(String* rhs) {
    label_ = rhs;
  }

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

  auto GetLabel() const -> String* {
    return label_;
  }

  inline auto HasLabel() const -> bool {
    return GetLabel() != nullptr;
  }

  virtual void ClearSuccessors() {
    // do nothing
  }

  virtual void ClearPredecessors() {
    // do nothing
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
  }                                                                      \
  void Clear##Name##s() override {                                       \
    List.clear();                                                        \
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

  auto GetPhis() -> PhiList& {
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

  HAS_SUCCESSOR_LIST;
  HAS_PREDECESSOR_LIST;
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

class LabelInstr : public Instruction {
 private:
  String* label_;

 public:
  explicit LabelInstr(String* label) :
    label_(label) {}
  ~LabelInstr() override = default;

  auto GetLabel() const -> String* {
    return label_;
  }

  inline auto HasLabel() const -> bool {
    return GetLabel() != nullptr;
  }

  DECLARE_INSTRUCTION_TYPE(Label);

 public:
  static inline auto New(String* label) -> LabelInstr* {
    return new LabelInstr(label);
  }
};

class SeqAppendInstr : public TemplateDefinition<2> {
 public:
  SeqAppendInstr(Value* seq, Value* item) {
    SetSeq(seq);
    SetItem(item);
  }

  HAS_NAMED_INPUT(Seq, 0);
  HAS_NAMED_INPUT(Item, 1);
  DECLARE_INSTRUCTION_TYPE(SeqAppend);

 public:
  static inline auto New(Value* seq, Value* item) -> SeqAppendInstr* {
    return new SeqAppendInstr(seq, item);
  }
};

class UpdateRecordInstr : public Definition {
 public:
  struct Entry {
    Property* property;
    Value* value;
  };

 private:
  Value* base_;
  std::vector<Entry> entries_;
  bool move_base_ = false;

 public:
  UpdateRecordInstr(Value* base, std::vector<Entry> entries) :
    base_(base),
    entries_(std::move(entries)) {}
  auto GetBase() const -> Value* {
    return base_;
  }

  auto GetEntries() const -> const std::vector<Entry>& {
    return entries_;
  }

  auto IsMoveBase() const -> bool {
    return move_base_;
  }

  void SetMoveBase(bool move = true) {
    move_base_ = move;
  }

  auto GetNumberOfInputs() const -> uint64_t override {
    return 1 + entries_.size();
  }

  auto GetInputAt(uint64_t idx) const -> Value* override {
    return idx == 0 ? base_ : entries_[idx - 1].value;
  }

  void SetInputAt(uint64_t idx, Value* rhs) override {
    if (idx == 0)
      base_ = rhs;
    else
      entries_[idx - 1].value = rhs;
  }

  DECLARE_INSTRUCTION_TYPE(UpdateRecord);

 public:
  static inline auto New(Value* base, std::vector<Entry> entries) -> UpdateRecordInstr* {
    return new UpdateRecordInstr(base, std::move(entries));
  }
};

class RetainInstr : public TemplateInstruction<1> {
 public:
  explicit RetainInstr(Value* val) {
    SetValue(val);
  }

  HAS_NAMED_INPUT(Value, 0);
  DECLARE_INSTRUCTION_TYPE(Retain);

 public:
  static inline auto New(Value* val) -> RetainInstr* {
    return new RetainInstr(val);
  }
};

class ReleaseInstr : public TemplateInstruction<1> {
 public:
  explicit ReleaseInstr(Value* val) {
    SetValue(val);
  }

  HAS_NAMED_INPUT(Value, 0);
  DECLARE_INSTRUCTION_TYPE(Release);

 public:
  static inline auto New(Value* val) -> ReleaseInstr* {
    return new ReleaseInstr(val);
  }
};

class NewColorInstr : public TemplateDefinition<4> {
 private:
  ColorModel model_;

 public:
  NewColorInstr(const ColorModel model, Value* c0, Value* c1, Value* c2, Value* a) :
    model_(model) {
    SetC0(c0);
    SetC1(c1);
    SetC2(c2);
    SetAlpha(a);
  }
  ~NewColorInstr() override = default;

  auto GetModel() const -> ColorModel {
    return model_;
  }

  HAS_NAMED_INPUT(C0, 0);
  HAS_NAMED_INPUT(C1, 1);
  HAS_NAMED_INPUT(C2, 2);
  HAS_NAMED_INPUT(Alpha, 3);
  DECLARE_INSTRUCTION_TYPE(NewColor);

 public:
  static inline auto New(const ColorModel model, Value* c0, Value* c1, Value* c2, Value* a) -> NewColorInstr* {
    return new NewColorInstr(model, c0, c1, c2, a);
  }
};

class CastColorInstr : public TemplateDefinition<1> {
 private:
  ColorModel target_;

 public:
  CastColorInstr(const ColorModel target, Value* value) :
    target_(target) {
    SetValue(value);
  }

  auto GetTarget() const -> ColorModel {
    return target_;
  }

  HAS_NAMED_INPUT(Value, 0);
  DECLARE_INSTRUCTION_TYPE(CastColor);

 public:
  static inline auto New(const ColorModel target, Value* value) -> CastColorInstr* {
    return new CastColorInstr(target, value);
  }
};

// `assert(cond, message)`. When the condition is a comparison, `a == b`, the two sides are values of their own, so a
// failure can say what they were (left and right are the sides, and the condition is the comparison of them); for any
// other condition they are the condition again, and `HasOperands` is false.
class AssertInstr : public TemplateInstruction<4> {
 private:
  std::string file_{};
  uint32_t line_ = 0;
  uint32_t column_ = 0;
  uint32_t length_ = 0;
  bool fail_fast_ = false;
  bool has_operands_ = false;
  BinaryOp op_ = BinaryOp::kEq;

 public:
  AssertInstr(Value* cond, Value* message = nullptr) {
    SetCond(cond);
    if (message)
      SetMessage(message);
    SetLeft(cond);
    SetRight(cond);
  }
  ~AssertInstr() override = default;

  auto GetFile() const -> std::string_view {
    return file_;
  }

  void SetFile(const std::string rhs) {
    file_ = rhs;
  }

  void SetFile(const std::string_view rhs) {
    file_ = rhs;
  }

  auto GetLine() const -> uint32_t {
    return line_;
  }

  void SetLine(const uint32_t rhs) {
    line_ = rhs;
  }

  auto GetColumn() const -> uint32_t {
    return column_;
  }

  void SetColumn(const uint32_t rhs) {
    column_ = rhs;
  }

  auto GetLength() const -> uint32_t {
    return length_;
  }

  void SetLength(const uint32_t rhs) {
    length_ = rhs;
  }

  auto ShouldFailFast() const -> bool {
    return fail_fast_;
  }

  void SetFailFast(const bool rhs) {
    fail_fast_ = rhs;
  }

  auto HasOperands() const -> bool {
    return has_operands_;
  }

  auto GetOp() const -> BinaryOp {
    return op_;
  }

  void SetOperands(const BinaryOp op, Value* left, Value* right) {
    has_operands_ = true;
    op_ = op;
    SetLeft(left);
    SetRight(right);
  }

  HAS_NAMED_INPUT(Cond, 0);
  HAS_NAMED_INPUT(Message, 1);
  HAS_NAMED_INPUT(Left, 2);
  HAS_NAMED_INPUT(Right, 3);
  DECLARE_INSTRUCTION_TYPE(Assert);

 public:
  static inline auto New(Value* cond, Value* message = nullptr) -> AssertInstr* {
    return new AssertInstr(cond, message);
  }
};

class PanicInstr : public TemplateInstruction<1> {
 public:
  explicit PanicInstr(Value* message) {
    if (message)
      SetMessage(message);
  }
  ~PanicInstr() override = default;

  HAS_NAMED_INPUT(Message, 0);
  DECLARE_INSTRUCTION_TYPE(Panic);

 public:
  static inline auto New(Value* rhs) -> PanicInstr* {
    return new PanicInstr(rhs);
  }
};

class CastInstr : public TemplateDefinition<1> {
 private:
  TypeId from_;
  TypeId to_;

 public:
  CastInstr(TypeId from, TypeId to, Value* value) {
    SetValue(value);
  }

  auto GetFromType() const -> TypeId {
    return from_;
  }

  void SetFromType(const TypeId rhs) {
    from_ = rhs;
  }

  auto GetToType() const -> TypeId {
    return to_;
  }

  void SetToType(const TypeId rhs) {
    to_ = rhs;
  }

  HAS_NAMED_INPUT(Value, 0);
  DECLARE_INSTRUCTION_TYPE(Cast);

 public:
  static inline auto New(TypeId from, TypeId to, Value* rhs) -> CastInstr* {
    return new CastInstr(from, to, rhs);
  }
};

class NewTupleInstr : public New {
 private:
  std::vector<bool> spreads_{};

 public:
  NewTupleInstr(const ValueList inputs, std::vector<bool> spreads = {}) :
    inputs_(std::move(inputs)),
    spreads_(std::move(spreads)) {}
  ~NewTupleInstr() override = default;

  auto IsSpreadAt(const uint64_t rhs) const -> bool {
    return rhs < spreads_.size() && spreads_[rhs];
  }

  HAS_INPUT_LIST;
  DECLARE_INSTRUCTION_TYPE(NewTuple);

 public:
  static inline auto New(const ValueList values, std::vector<bool> spreads = {}) -> NewTupleInstr* {
    return new NewTupleInstr(std::move(values), std::move(spreads));
  }
};

class SliceTupleInstr : public New {
 private:
  Value* value_;
  uint64_t prefix_;
  uint64_t suffix_;

 public:
  SliceTupleInstr(Value* value, const uint64_t prefix, const uint64_t suffix) :
    value_(value),
    prefix_(prefix),
    suffix_(suffix) {}
  ~SliceTupleInstr() override = default;

  auto GetValue() const -> Value* {
    return value_;
  }

  auto GetPrefix() const -> uint64_t {
    return prefix_;
  }

  auto GetSuffix() const -> uint64_t {
    return suffix_;
  }

  auto GetNumberOfInputs() const -> uint64_t override {
    return 1;
  }

  auto GetInputAt(const uint64_t rhs) const -> Value* override {
    return rhs == 0 ? value_ : nullptr;
  }

  void SetInputAt(const uint64_t idx, Value* rhs) override {
    if (idx == 0)
      value_ = rhs;
  }

  DECLARE_INSTRUCTION_TYPE(SliceTuple);

 public:
  static inline auto New(Value* value, const uint64_t prefix, const uint64_t suffix) -> SliceTupleInstr* {
    return new SliceTupleInstr(value, prefix, suffix);
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
