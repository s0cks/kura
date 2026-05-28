#ifndef KURA_IR_H
#define KURA_IR_H

#include <functional>
#include <string>

namespace kura {
#define FOR_EACH_INSTRUCTION(V) \
  V(Constant)                   \
  V(UnaryOp)                    \
  V(BinaryOp)                   \
  V(Call)

class Instruction;
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

  virtual auto GetInstructionName() const -> std::string_view = 0;
  virtual auto Accept(InstructionVisitor* vis) -> bool = 0;

  virtual auto VisitChildren(InstructionVisitor* vis) -> bool {
    return true;
  }

  virtual auto VisitChildren(std::function<bool(Instruction*)> vis) -> bool {
    return true;
  }

#define DEFINE_TYPE_CHECK(Name)     \
  auto As##Name() -> Name##Instr* { \
    return nullptr;                 \
  }                                 \
  auto Is##Name() -> bool {         \
    return As##Name() != nullptr;   \
  }
  FOR_EACH_INSTRUCTION(DEFINE_TYPE_CHECK)
#undef DEFINE_TYPE_CHECK
};

#define DECLARE_INSTRUCTION_TYPE(Name)                           \
 public:                                                         \
  auto Accept(InstructionVisitor* vis) -> bool override;         \
  auto GetInstructionName() const -> std::string_view override { \
    return #Name;                                                \
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

class ConstantInstr : public Instruction {
 public:
  ConstantInstr() = default;
  ~ConstantInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(Constant);

 public:
  static inline auto New() -> ConstantInstr* {
    return new ConstantInstr();
  }
};

class UnaryOpInstr : public Instruction {
 public:
  UnaryOpInstr() = default;
  ~UnaryOpInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(UnaryOpInstr);

 public:
  static inline auto New() -> UnaryOpInstr* {
    return new UnaryOpInstr();
  }
};

class BinaryOpInstr : public Instruction {
 public:
  BinaryOpInstr() = default;
  ~BinaryOpInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(BinaryOp);

 public:
  static inline auto New() -> BinaryOpInstr* {
    return new BinaryOpInstr();
  }
};

class CallInstr : public Instruction {
 public:
  CallInstr() = default;
  ~CallInstr() override = default;

  DECLARE_INSTRUCTION_TYPE(CallInstr);

 public:
  static inline auto New() -> CallInstr* {
    return new CallInstr();
  }
};
}  // namespace kura

#endif  // KURA_IR_H
