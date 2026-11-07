#ifndef KURA_BACKEND_VALUE_VISITOR_H
#define KURA_BACKEND_VALUE_VISITOR_H

#include "backend/effect_visitor.h"

namespace kura {
class ValueVisitor : public EffectVisitor {
 private:
  Value* value_ = nullptr;

 protected:
  auto WantsValue() const -> bool override {
    return true;
  }

  void ReturnValue(Value* value) override {
    value_ = value;
  }

 public:
  explicit ValueVisitor(FlowGraphBuilder* owner) :
    EffectVisitor(owner) {}
  ~ValueVisitor() override = default;

  auto GetValue() const -> Value* {
    return value_;
  }

  inline auto HasValue() const -> bool {
    return GetValue() != nullptr;
  }
};
}  // namespace kura

#endif  // KURA_BACKEND_VALUE_VISITOR_H
