#ifndef KURA_EXPR_H
#define KURA_EXPR_H

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "binary_op.h"
#include "common.h"
#include "dom/kind.h"
#include "frontend/type_expr.h"
#include "object.h"
#include "property.h"
#include "unary_op.h"

namespace kura {
class Function;
class LocalVariable;  // TODO(@s0cks): remove
}  // namespace kura

namespace kura::expr {
#define FOR_EACH_EXPR(V) \
  V(Literal)             \
  V(Unary)               \
  V(Binary)              \
  V(Seq)                 \
  V(If)                  \
  V(Case)                \
  V(Match)               \
  V(Node)                \
  V(Pipeline)            \
  V(List)                \
  V(ListComprehension)   \
  V(Record)              \
  V(Spread)              \
  V(LoadLocal)           \
  V(StoreLocal)          \
  V(GetProperty)         \
  V(StoreProperty)       \
  V(WildcardPattern)     \
  V(LiteralPattern)      \
  V(BindingPattern)      \
  V(VariantPattern)      \
  V(Lambda)              \
  V(Fold)                \
  V(Variant)             \
  V(Call)

class Expr;
// clang-format off
#define DECLARE_EXPR(Name) \
  class Name##Expr;
FOR_EACH_EXPR(DECLARE_EXPR)
#undef DECLARE_EXPR
// clang-format on

class ExprVisitor {
 public:
  ExprVisitor() = default;
  virtual ~ExprVisitor() = default;

  // clang-format off
#define DEFINE_VISIT(Name) \
  virtual auto Visit##Name(Name##Expr*) -> VisitResult = 0;
  FOR_EACH_EXPR(DEFINE_VISIT)
#undef DEFINE_VISIT
  // clang-format on
};

#define DECLARE_EXPR_TYPE(Name)                          \
 public:                                                 \
  auto GetName() const -> std::string_view override {    \
    return #Name;                                        \
  }                                                      \
  auto Accept(ExprVisitor* vis) -> VisitResult override; \
  auto As##Name() -> Name##Expr* override {              \
    return this;                                         \
  }

class Expr {
 protected:
  Expr() = default;

  virtual void SetChildAt(const uint64_t idx, Expr* value) {
    // do nothing
  }

 public:
  virtual ~Expr() = default;
  virtual auto GetName() const -> std::string_view = 0;

  virtual auto GetNumberOfChildren() const -> uint64_t {
    return 0;
  }

  virtual auto GetChildAt(const uint64_t idx) const -> Expr* {
    return nullptr;
  }

  virtual auto HasChildAt(const uint64_t idx) const -> bool {
    return false;
  }

  virtual auto HasChildren() const -> bool {
    return false;
  }

  virtual auto VisitChildren(ExprVisitor* vis) -> VisitResult {
    return VisitResult::kContinue;
  }

  virtual auto VisitChildren(const std::function<VisitResult(Expr*)> vis) -> VisitResult {
    return VisitResult::kContinue;
  }

  virtual auto Accept(ExprVisitor* vis) -> VisitResult = 0;
#define DEFINE_TYPE_CHECK(Name)            \
  virtual auto As##Name() -> Name##Expr* { \
    return nullptr;                        \
  }                                        \
  auto Is##Name() -> bool {                \
    return As##Name() != nullptr;          \
  }
  FOR_EACH_EXPR(DEFINE_TYPE_CHECK)
#undef DEFINE_TYPE_CHECK
};

using ExprList = std::vector<Expr*>;

template <const size_t NumberOfChildren>
class TemplateExpr : public Expr {
 private:
  std::array<Expr*, NumberOfChildren> children_{};

 protected:
  TemplateExpr() = default;

  void SetChildAt(const uint64_t idx, Expr* value) override {
    children_.at(idx) = value;
  }

 public:
  ~TemplateExpr() override = default;

  auto GetChildAt(const uint64_t idx) const -> Expr* override {
    if (idx >= children_.size())
      return nullptr;
    return children_.at(idx);
  }

  auto HasChildAt(const uint64_t idx) const -> bool override {
    return GetChildAt(idx) != nullptr;
  }

  auto HasChildren() const -> bool override {
    for (auto idx = 0; idx < NumberOfChildren; idx++) {
      if (HasChildAt(idx))
        return true;
    }
    return false;
  }

  auto GetNumberOfChildren() const -> uint64_t override {
    return NumberOfChildren;
  }

  auto VisitChildren(ExprVisitor* vis) -> VisitResult override {
    for (auto idx = 0; idx < NumberOfChildren; idx++) {
      const auto child = GetChildAt(idx);
      if (!child)
        continue;
      if (!children_[idx]->Accept(vis))
        return VisitResult::kStop;
    }
    return VisitResult::kContinue;
  }

  auto VisitChildren(const std::function<VisitResult(Expr*)> vis) -> VisitResult override {
    for (auto idx = 0; idx < NumberOfChildren; idx++) {
      if (!children_[idx])
        continue;
      if (!vis(children_[idx]))
        return VisitResult::kStop;
    }
    return VisitResult::kContinue;
  }
};

#define _HAS_NAMED_INPUT_LIST(Name, MultiName, Type, Member)    \
 public:                                                        \
  using Name##List = std::vector<Type*>;                        \
                                                                \
 private:                                                       \
  Name##List Member{};                                          \
                                                                \
 protected:                                                     \
  void Append##Name(Type* rhs) {                                \
    Member.push_back(rhs);                                      \
  }                                                             \
                                                                \
 public:                                                        \
  auto Get##MultiName() const -> const Name##List& {            \
    return Member;                                              \
  }                                                             \
  auto GetNumberOf##MultiName() const -> uint64_t {             \
    return Member.size();                                       \
  }                                                             \
  auto Has##MultiName() const -> bool {                         \
    return !Member.empty();                                     \
  }                                                             \
  auto Get##Name##At(const uint64_t idx) const -> Type* {       \
    if (idx >= Member.size())                                   \
      return nullptr;                                           \
    return Member.at(idx);                                      \
  }                                                             \
  inline auto Has##Name##At(const uint64_t idx) const -> bool { \
    return Get##Name##At(idx) != nullptr;                       \
  }                                                             \
  auto VisitAll##Name##s(ExprVisitor* vis) -> VisitResult {     \
    for (auto idx = 0; idx < Member.size(); idx++) {            \
      const auto child = Member.at(idx);                        \
      if (!child)                                               \
        continue;                                               \
      if (!child->Accept(vis))                                  \
        return false;                                           \
    }                                                           \
    return true;                                                \
  }

class DynamicTemplateExpr : public Expr {
  friend class UIExprBuilder;

 private:
  ExprList children_{};

 protected:
  explicit DynamicTemplateExpr(const ExprList children = {}) :
    children_(std::move(children)) {}

  void Append(Expr* child) {
    children_.push_back(child);
  }

 public:
  ~DynamicTemplateExpr() override = default;

  auto GetChildren() const -> const ExprList& {
    return children_;
  }

  auto HasChildren() const -> bool override {
    return !children_.empty();
  }

  auto GetNumberOfChildren() -> const uint64_t {
    return children_.size();
  }

  auto GetChildAt(const uint64_t idx) const -> Expr* override {
    return children_[idx];
  }

  auto HasChildAt(const uint64_t idx) const -> bool override {
    return idx <= children_.size() && children_[idx] != nullptr;
  }

  auto GetLast() const -> Expr* {
    return children_.back();
  }

  auto VisitChildren(ExprVisitor* vis) -> VisitResult override {
    for (const auto& child : children_) {
      if (!child->Accept(vis))
        return VisitResult::kStop;
    }
    return VisitResult::kContinue;
  }

  auto VisitChildren(const std::function<VisitResult(Expr*)> vis) -> VisitResult override {
    for (const auto& child : children_) {
      if (!vis(child))
        return VisitResult::kStop;
    }
    return VisitResult::kContinue;
  }
};

#define _HAS_NAMED_INPUT(Name, Type, Position)                \
  static constexpr const auto k##Name##Pos = (Position);      \
                                                              \
 private:                                                     \
  inline void Set##Name(Type* rhs) {                          \
    return SetChildAt(k##Name##Pos, rhs);                     \
  }                                                           \
                                                              \
 public:                                                      \
  inline auto Get##Name() const -> Type* {                    \
    return reinterpret_cast<Type*>(GetChildAt(k##Name##Pos)); \
  }                                                           \
  inline auto Has##Name() const -> bool {                     \
    return Get##Name() != nullptr;                            \
  }

#define HAS_NAMED_INPUT(Name, Position)       _HAS_NAMED_INPUT(Name, Expr, Position)

#define HAS_NAMED_TYPED_INPUT(Name, Position) _HAS_NAMED_INPUT(Name, Name##Expr, Position)

class SeqExpr : public DynamicTemplateExpr {
 public:
  explicit SeqExpr(const ExprList children = {}) :
    DynamicTemplateExpr(std::move(children)) {}
  ~SeqExpr() override = default;

  DECLARE_EXPR_TYPE(Seq);

 public:
  static inline auto New(const ExprList children = {}) -> SeqExpr* {
    return new SeqExpr(std::move(children));
  }

  static inline auto New(Expr* child) -> SeqExpr* {
    return New(ExprList{child});
  }
};

class LiteralExpr : public Expr {
 private:
  Object* value_;

 public:
  explicit LiteralExpr(Object* value) :
    value_(value) {}
  ~LiteralExpr() override = default;

  auto GetValue() const -> Object* {
    return value_;
  }

  DECLARE_EXPR_TYPE(Literal);

 public:
  static inline auto New(Object* value) -> Expr* {
    return new LiteralExpr(value);
  }
};

class StorePropertyExpr : public TemplateExpr<1> {
 private:
  Property* property_;

  void SetProperty(Property* rhs) {
    property_ = rhs;
  }

 public:
  explicit StorePropertyExpr(Property* property, Expr* value = nullptr) {
    SetProperty(property);
    SetValue(value);
  }
  ~StorePropertyExpr() override = default;

  auto GetProperty() const -> Property* {
    return property_;
  }

  HAS_NAMED_INPUT(Value, 0);
  DECLARE_EXPR_TYPE(StoreProperty);

 public:
  static inline auto New(Property* property, Expr* value) -> StorePropertyExpr* {
    return new StorePropertyExpr(property, value);
  }
};

class GetPropertyExpr : public TemplateExpr<1> {
  static constexpr const auto kInstancePos = 0;

 private:
  Property* property_;
  bool safe_access_;

  inline void SetInstance(Expr* rhs) {
    return SetChildAt(kInstancePos, rhs);
  }

 public:
  GetPropertyExpr(Expr* instance, Property* property, const bool safe_access) :
    property_(property),
    safe_access_(safe_access) {
    SetInstance(instance);
  }
  ~GetPropertyExpr() override = default;

  auto GetProperty() const -> Property* {
    return property_;
  }

  inline auto GetInstance() -> Expr* {
    return GetChildAt(kInstancePos);
  }

  auto IsSafeAccess() const -> bool {
    return safe_access_;
  }

  DECLARE_EXPR_TYPE(GetProperty);

 public:
  static inline auto New(Expr* instance, Property* property, const bool safe_access = false) -> Expr* {
    return new GetPropertyExpr(instance, property, safe_access);
  }
};

class LoadLocalExpr : public Expr {
 private:
  LocalVariable* local_;

 public:
  LoadLocalExpr(LocalVariable* local) :
    local_(local) {}
  ~LoadLocalExpr() override = default;

  auto GetLocal() const -> LocalVariable* {
    return local_;
  }

  DECLARE_EXPR_TYPE(LoadLocal);

 public:
  static inline auto New(LocalVariable* local) -> Expr* {
    return new LoadLocalExpr(local);
  }
};

class StoreLocalExpr : public TemplateExpr<1> {
 private:
  LocalVariable* local_;
  TypeExpr* annotation_ = nullptr;  // `let x: Number = ...`
  DirectiveList directives_{};


  inline void SetLocal(LocalVariable* local) {
    local_ = local;
  }

 public:
  StoreLocalExpr(LocalVariable* local, Expr* value) {
    SetLocal(local);
    SetValue(value);
  }
  ~StoreLocalExpr() override = default;

  auto GetLocal() const -> LocalVariable* {
    return local_;
  }

  auto GetAnnotation() const -> TypeExpr* {
    return annotation_;
  }

  void SetAnnotation(TypeExpr* rhs) {
    annotation_ = rhs;
  }

  auto GetDirectives() const -> const DirectiveList& {
    return directives_;
  }

  void SetDirectives(const DirectiveList rhs) {
    directives_ = std::move(rhs);
  }

  HAS_NAMED_INPUT(Value, 0);
  DECLARE_EXPR_TYPE(StoreLocal);

 public:
  static inline auto New(LocalVariable* local, Expr* value) -> StoreLocalExpr* {
    return new StoreLocalExpr(local, value);
  }
};

class UnaryExpr : public TemplateExpr<1> {
 private:
  UnaryOp op_;

 public:
  UnaryExpr(const UnaryOp op, Expr* value) :
    op_(op) {
    SetChildAt(kValuePos, value);
  }
  ~UnaryExpr() override = default;

  auto GetOp() const -> UnaryOp {
    return op_;
  }

#define DEFINE_OP_CHECK(Name)            \
  inline auto Is##Name() const -> bool { \
    return GetOp() == UnaryOp::k##Name;  \
  }
  FOR_EACH_UNARY_OP(DEFINE_OP_CHECK)
#undef DEFINE_OP_CHECK

  HAS_NAMED_INPUT(Value, 0);
  DECLARE_EXPR_TYPE(Unary);

 public:
  static inline auto New(const UnaryOp op, Expr* value) -> Expr* {
    return new UnaryExpr(op, value);
  }

#define DEFINE_NEW(Name)                               \
  static inline auto New##Name(Expr* value) -> Expr* { \
    return New(UnaryOp::k##Name, value);               \
  }
  FOR_EACH_UNARY_OP(DEFINE_NEW)
#undef DEFINE_NEW
};

class BinaryExpr : public TemplateExpr<2> {
 private:
  BinaryOp op_;

 public:
  BinaryExpr(const BinaryOp op, Expr* left, Expr* right) :
    op_(op) {
    SetLeft(left);
    SetRight(right);
  }
  ~BinaryExpr() override = default;

  auto GetOp() const -> BinaryOp {
    return op_;
  }

#define DEFINE_OP_CHECK(Name)            \
  inline auto Is##Name() const -> bool { \
    return GetOp() == BinaryOp::k##Name; \
  }
  FOR_EACH_BINARY_OP(DEFINE_OP_CHECK)
#undef DEFINE_OP_CHECK

  HAS_NAMED_INPUT(Left, 0);
  HAS_NAMED_INPUT(Right, 1);
  DECLARE_EXPR_TYPE(Binary);

 public:
  static inline auto New(const BinaryOp op, Expr* lhs, Expr* rhs) -> Expr* {
    return new BinaryExpr(op, lhs, rhs);
  }

#define DEFINE_NEW(Name)                                        \
  static inline auto New##Name(Expr* lhs, Expr* rhs) -> Expr* { \
    return New(BinaryOp::k##Name, lhs, rhs);                    \
  }
  FOR_EACH_BINARY_OP(DEFINE_NEW)
#undef DEFINE_NEW
};

class SpreadExpr : public TemplateExpr<1> {
 public:
  explicit SpreadExpr(Expr* value) {
    SetValue(value);
  }
  ~SpreadExpr() override = default;

  HAS_NAMED_INPUT(Value, 0);
  DECLARE_EXPR_TYPE(Spread);

 public:
  static inline auto New(Expr* value) -> Expr* {
    return new SpreadExpr(value);
  }
};

class IfExpr : public TemplateExpr<3> {
 public:
  IfExpr(Expr* condition, Expr* then_expr, Expr* else_expr) {
    SetCondition(condition);
    SetThen(then_expr);
    SetElse(else_expr);
  }
  ~IfExpr() override = default;

  HAS_NAMED_INPUT(Condition, 0);
  HAS_NAMED_INPUT(Then, 1);  // TODO(@s0cks): convert to SeqExpr
  HAS_NAMED_INPUT(Else, 2);  // TODO(@s0cks): convert to SeqExpr
  DECLARE_EXPR_TYPE(If);

 public:
  static inline auto New(Expr* condition, Expr* then_expr, Expr* else_expr = nullptr) -> Expr* {
    return new IfExpr(condition, then_expr, else_expr);
  }
};

class PipelineExpr : public DynamicTemplateExpr {
 public:
  PipelineExpr(const ExprList children) :
    DynamicTemplateExpr(std::move(children)) {}
  ~PipelineExpr() override = default;

  DECLARE_EXPR_TYPE(Pipeline);
};

class PatternExpr : public Expr {
 protected:
  PatternExpr() = default;

 public:
  ~PatternExpr() override = default;
};

template <const uint64_t NumberOfChildren>
class TemplatePatternExpr : public PatternExpr {
 private:
  Expr* children_[NumberOfChildren];

 protected:
  TemplatePatternExpr() = default;

  void SetChildAt(const uint64_t idx, Expr* value) override {
    children_[idx] = value;
  }

 public:
  ~TemplatePatternExpr() override = default;

  auto GetChildAt(const uint64_t idx) const -> Expr* override {
    return children_[idx];
  }

  auto HasChildAt(const uint64_t idx) const -> bool override {
    return children_[idx] != nullptr;
  }

  auto HasChildren() const -> bool override {
    for (auto idx = 0; idx < NumberOfChildren; idx++) {
      if (HasChildAt(idx))
        return true;
    }
    return false;
  }

  auto GetNumberOfChildren() const -> uint64_t override {
    return NumberOfChildren;
  }

  auto VisitChildren(ExprVisitor* vis) -> VisitResult override {
    for (auto idx = 0; idx < NumberOfChildren; idx++) {
      if (!children_[idx])
        continue;

      if (!children_[idx]->Accept(vis))
        return false;
    }

    return true;
  }

  auto VisitChildren(const std::function<VisitResult(Expr*)> vis) -> VisitResult override {
    for (auto idx = 0; idx < NumberOfChildren; idx++) {
      if (!children_[idx])
        continue;

      if (!vis(children_[idx]))
        return false;
    }

    return true;
  }
};

class WildcardPatternExpr : public PatternExpr {
 public:
  WildcardPatternExpr() = default;
  ~WildcardPatternExpr() override = default;

  DECLARE_EXPR_TYPE(WildcardPattern);

 public:
  static inline auto New() -> PatternExpr* {
    return new WildcardPatternExpr();
  }
};

class LiteralPatternExpr : public TemplatePatternExpr<1> {
 public:
  explicit LiteralPatternExpr(LiteralExpr* value) {
    SetLiteral(value);
  }
  ~LiteralPatternExpr() override = default;

  HAS_NAMED_TYPED_INPUT(Literal, 0);
  DECLARE_EXPR_TYPE(LiteralPattern);

 public:
  static inline auto New(LiteralExpr* value) -> PatternExpr* {
    return new LiteralPatternExpr(value);
  }
};

// `name` in a match arm: matches anything and binds it for the arm body.
class BindingPatternExpr : public PatternExpr {
 private:
  LocalVariable* local_;

 public:
  explicit BindingPatternExpr(LocalVariable* local) :
    local_(local) {}
  ~BindingPatternExpr() override = default;

  auto GetLocal() const -> LocalVariable* {
    return local_;
  }

  DECLARE_EXPR_TYPE(BindingPattern);

 public:
  static inline auto New(LocalVariable* local) -> PatternExpr* {
    return new BindingPatternExpr(local);
  }
};

// `Click(position)`, `Tick`, `Some(Pair(a, b))`. The payload pattern is optional.
class VariantPatternExpr : public TemplatePatternExpr<1> {
 private:
  std::string name_;

 public:
  VariantPatternExpr(const std::string name, PatternExpr* payload) :
    name_(std::move(name)) {
    SetPayload(payload);
  }
  ~VariantPatternExpr() override = default;

  auto GetVariantName() const -> const std::string& {
    return name_;
  }

  _HAS_NAMED_INPUT(Payload, PatternExpr, 0);
  DECLARE_EXPR_TYPE(VariantPattern);

 public:
  static inline auto New(const std::string name, PatternExpr* payload = nullptr) -> PatternExpr* {
    return new VariantPatternExpr(std::move(name), payload);
  }
};

class CaseExpr : public TemplateExpr<2> {
 public:
  CaseExpr(PatternExpr* pattern, Expr* body) {
    SetPattern(pattern);
    SetBody(body);
  }
  ~CaseExpr() override = default;

  HAS_NAMED_TYPED_INPUT(Pattern, 0);
  HAS_NAMED_INPUT(Body, 1);
  DECLARE_EXPR_TYPE(Case);

 public:
  static inline auto New(PatternExpr* pattern, Expr* body) -> Expr* {
    return new CaseExpr(pattern, body);
  }
};

using CaseList = std::vector<CaseExpr*>;

class MatchExpr : public TemplateExpr<2> {
  friend class ExprBuilder;
  friend class MatchExprBuilder;

 public:
  explicit MatchExpr(Expr* subject) {
    SetSubject(subject);
  }
  explicit MatchExpr(Expr* subject, const CaseList cases) :
    cases_(std::move(cases)) {
    SetSubject(subject);
  }
  ~MatchExpr() override = default;

  HAS_NAMED_INPUT(Subject, 0);
  HAS_NAMED_INPUT(Wildcard, 1);
  _HAS_NAMED_INPUT_LIST(Case, Cases, CaseExpr, cases_);
  DECLARE_EXPR_TYPE(Match);

 public:
  static inline auto New(Expr* subject) -> Expr* {
    return new MatchExpr(subject);
  }

  static inline auto New(Expr* subject, const CaseList cases) -> Expr* {
    return new MatchExpr(subject, std::move(cases));
  }
};

class ListExpr : public DynamicTemplateExpr {
 public:
  explicit ListExpr(const ExprList children) :
    DynamicTemplateExpr(std::move(children)) {}
  ~ListExpr() override = default;

  DECLARE_EXPR_TYPE(List);

 public:
  static inline auto New(const ExprList children) -> Expr* {
    return new ListExpr(std::move(children));
  }
};

class ListComprehensionExpr : public TemplateExpr<3> {
 private:
  LocalVariable* local_;
  std::string binding_{};

  inline void SetBinding(const std::string value) {
    binding_ = std::move(value);
  }

 public:
  // `local` is the loop variable: it is visible to the clause and the body.
  ListComprehensionExpr(Expr* iterable, Expr* body, LocalVariable* local, const std::string binding,
                        Expr* clause = nullptr) :
    local_(local) {
    SetIterable(iterable);
    SetBinding(std::move(binding));
    SetBody(body);
    SetClause(clause);
  }
  ~ListComprehensionExpr() override = default;

  auto GetLocal() const -> LocalVariable* {
    return local_;
  }

  auto GetBinding() const -> const std::string& {
    return binding_;
  }

  HAS_NAMED_INPUT(Iterable, 0);
  HAS_NAMED_INPUT(Body, 1);
  HAS_NAMED_INPUT(Clause, 2);
  DECLARE_EXPR_TYPE(ListComprehension);

 public:
  static inline auto New(Expr* iterable, Expr* body, LocalVariable* local, const std::string binding,
                         Expr* clause = nullptr) -> Expr* {
    return new ListComprehensionExpr(iterable, body, local, std::move(binding), clause);
  }
};

// `(x, y) => x + y`
class LambdaExpr : public TemplateExpr<1> {
 public:
  using ParamList = std::vector<LocalVariable*>;

 private:
  ParamList params_;

 public:
  LambdaExpr(const ParamList params, Expr* body) :
    params_(std::move(params)) {
    SetBody(body);
  }
  ~LambdaExpr() override = default;

  auto GetParams() const -> const ParamList& {
    return params_;
  }

  auto GetNumberOfParams() const -> uint64_t {
    return params_.size();
  }

  HAS_NAMED_INPUT(Body, 0);
  DECLARE_EXPR_TYPE(Lambda);

 public:
  static inline auto New(const ParamList params, Expr* body) -> Expr* {
    return new LambdaExpr(std::move(params), body);
  }
};

// `for item in items [if guard] fold acc = initial => body`
class FoldExpr : public TemplateExpr<4> {
 private:
  LocalVariable* binding_;
  LocalVariable* accumulator_;

 public:
  FoldExpr(Expr* iterable, LocalVariable* binding, LocalVariable* accumulator, Expr* initial, Expr* body,
           Expr* clause = nullptr) :
    binding_(binding),
    accumulator_(accumulator) {
    SetIterable(iterable);
    SetInitial(initial);
    SetBody(body);
    SetClause(clause);
  }
  ~FoldExpr() override = default;

  auto GetBinding() const -> LocalVariable* {
    return binding_;
  }

  auto GetAccumulator() const -> LocalVariable* {
    return accumulator_;
  }

  HAS_NAMED_INPUT(Iterable, 0);
  HAS_NAMED_INPUT(Initial, 1);
  HAS_NAMED_INPUT(Body, 2);
  HAS_NAMED_INPUT(Clause, 3);
  DECLARE_EXPR_TYPE(Fold);

 public:
  static inline auto New(Expr* iterable, LocalVariable* binding, LocalVariable* accumulator, Expr* initial, Expr* body,
                         Expr* clause = nullptr) -> Expr* {
    return new FoldExpr(iterable, binding, accumulator, initial, body, clause);
  }
};

// Constructs a variant: `Tick`, or `Click({ x: 1, y: 2, })` with a payload.
class VariantExpr : public TemplateExpr<1> {
 private:
  std::string name_;

 public:
  VariantExpr(const std::string name, Expr* payload) :
    name_(std::move(name)) {
    SetPayload(payload);
  }
  ~VariantExpr() override = default;

  auto GetVariantName() const -> const std::string& {
    return name_;
  }

  HAS_NAMED_INPUT(Payload, 0);
  DECLARE_EXPR_TYPE(Variant);

 public:
  static inline auto New(const std::string name, Expr* payload = nullptr) -> Expr* {
    return new VariantExpr(std::move(name), payload);
  }
};

class NodeExpr : public DynamicTemplateExpr {
  _HAS_NAMED_INPUT_LIST(Property, Properties, StorePropertyExpr, properties_);

 public:
  enum Kind {
#define DEFINE_KIND(Name, Tag) k##Name,
    FOR_EACH_ELEMENT_NODE(DEFINE_KIND)
#undef DEFINE_KIND
  };

  static inline auto ToString(const Kind& rhs) -> std::string_view {
    switch (rhs) {
#define DEFINE_CHECK(Name, Tag) \
  case Kind::k##Name:      \
    return #Name;
      FOR_EACH_ELEMENT_NODE(DEFINE_CHECK)
#undef DEFINE_CHECK
      default:
        return "Unknown";
    }
  }

  friend auto operator<<(std::ostream& stream, const Kind rhs) -> std::ostream& {
    return stream << ToString(rhs);
  }

 private:
  Kind kind_;
  DirectiveList directives_{};

 public:
  explicit NodeExpr(const Kind kind, const PropertyList properties) :
    DynamicTemplateExpr(),
    kind_(kind),
    properties_(std::move(properties)) {
    PropertyList props;
  }
  ~NodeExpr() override = default;

  auto GetKind() const -> Kind {
    return kind_;
  }

  auto GetDirectives() const -> const DirectiveList& {
    return directives_;
  }

  void SetDirectives(const DirectiveList rhs) {
    directives_ = std::move(rhs);
  }

#define DEFINE_TYPE_CHECK(Name, Tag)          \
  inline auto Is##Name() const -> bool { \
    return GetKind() == Kind::k##Name;   \
  }
  FOR_EACH_ELEMENT_NODE(DEFINE_TYPE_CHECK)
#undef DEFINE_TYPE_CHECK

  DECLARE_EXPR_TYPE(Node);

 public:
  static inline auto New(const Kind kind, const PropertyList properties = {}) -> Expr* {
    return new NodeExpr(kind, std::move(properties));
  }

#define DEFINE_NEW(Name, Tag)                                                      \
  static inline auto New##Name(const PropertyList properties = {}) -> Expr* { \
    return New(Kind::k##Name, std::move(properties));                         \
  }
  FOR_EACH_ELEMENT_NODE(DEFINE_NEW)
#undef DEFINE_NEW
};

class RecordExpr : public Expr {
  _HAS_NAMED_INPUT_LIST(Property, Properties, StorePropertyExpr, properties_);

 public:
  using SpreadList = std::vector<SpreadExpr*>;

 private:
  SpreadList spreads_{};
  ExprList entries_{};  // spreads and properties in source order

  inline void SetSpreadAt(const uint64_t idx, SpreadExpr* value) {
    spreads_[idx] = value;
  }

 public:
  RecordExpr(const SpreadList spreads, const PropertyList properties) :
    Expr(),
    spreads_(std::move(spreads)),
    properties_(std::move(properties)) {}
  ~RecordExpr() override = default;

  auto GetSpreads() const -> const SpreadList& {
    return spreads_;
  }

  // Spreads and properties interleaved as written. Later entries override
  // earlier ones, so the order is meaningful: `{ ...a, x: 1 }` != `{ x: 1, ...a }`.
  auto GetEntries() const -> const ExprList& {
    return entries_;
  }

  auto GetNumberOfSpreads() const -> uint64_t {
    return spreads_.size();
  }

  auto GetSpreadAt(const uint64_t idx) const -> SpreadExpr* {
    return spreads_[idx];
  }

  auto HasSpreadAt(const uint64_t idx) const -> bool {
    return idx < spreads_.size() && spreads_[idx] != nullptr;
  }

  auto GetNumberOfChildren() const -> uint64_t override {
    return GetNumberOfSpreads() + GetNumberOfProperties();
  }

  auto GetChildAt(const uint64_t idx) const -> Expr* override {
    if (idx > GetNumberOfSpreads())
      return GetPropertyAt(idx - GetNumberOfSpreads());
    return GetSpreadAt(idx);
  }

  auto HasChildren() const -> bool override {
    return !spreads_.empty() && !properties_.empty();
  }

  auto HasChildAt(const uint64_t idx) const -> bool override {
    return GetChildAt(idx) != nullptr;
  }

  auto VisitSpreads(ExprVisitor* vis) -> bool;
  auto VisitSpreads(const std::function<bool(Expr*)> vis) -> bool;
  auto VisitProperties(const std::function<bool(Expr*)> vis) -> bool;
  auto VisitChildren(ExprVisitor* vis) -> VisitResult override;
  auto VisitChildren(const std::function<VisitResult(Expr*)> vis) -> VisitResult override;

  DECLARE_EXPR_TYPE(Record);

 public:
  static inline auto New(const SpreadList spreads, const PropertyList properties) -> Expr* {
    return new RecordExpr(std::move(spreads), std::move(properties));
  }

  static inline auto NewOrdered(const ExprList entries) -> Expr* {
    SpreadList spreads{};
    PropertyList properties{};
    for (const auto entry : entries) {
      if (const auto spread = entry->AsSpread())
        spreads.push_back(spread);
      else if (const auto property = entry->AsStoreProperty())
        properties.push_back(property);
    }
    const auto result = new RecordExpr(std::move(spreads), std::move(properties));
    result->entries_ = std::move(entries);
    return result;
  }
};

class CallExpr : public TemplateExpr<1> {
 private:
  ExprList args_{};

  inline void SetArgAt(const uint64_t idx, Expr* value) {
    args_.at(idx) = value;
  }

 public:
  explicit CallExpr(Expr* target, const ExprList args) :
    args_(std::move(args)) {
    SetTarget(target);
  }
  ~CallExpr() override = default;

  auto GetArgs() const -> const ExprList& {
    return args_;
  }

  auto GetNumberOfArgs() const -> uint64_t {
    return args_.size();
  }

  auto GetArgAt(const uint64_t idx) const -> Expr* {
    return args_.at(idx);
  }

  auto HasArgAt(const uint64_t idx) const -> bool {
    return GetArgAt(idx) != nullptr;
  }

  auto GetNumberOfChildren() const -> uint64_t override {
    return 1 + GetNumberOfArgs();
  }

  auto GetChildAt(const uint64_t idx) const -> Expr* override {
    if (idx == 0)
      return TemplateExpr<1>::GetChildAt(0);
    return GetArgAt(idx - 1);
  }

  auto VisitChildren(ExprVisitor* vis) -> VisitResult override;

  HAS_NAMED_INPUT(Target, 0);
  DECLARE_EXPR_TYPE(Call);

 public:
  static inline auto New(Expr* target, const ExprList args = {}) -> Expr* {
    return new CallExpr(target, std::move(args));
  }
};

#undef HAS_NAMED_INPUT
#undef HAS_INPUT_LIST
}  // namespace kura::expr

#endif  // KURA_EXPR_H
