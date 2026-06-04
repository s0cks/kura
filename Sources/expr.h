#ifndef KURA_EXPR_H
#define KURA_EXPR_H

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "common.h"
#include "element.h"
#include "object.h"

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
  V(Match)               \
  V(Node)                \
  V(Pipeline)            \
  V(List)                \
  V(ListComprehension)   \
  V(Record)              \
  V(RecordProperty)      \
  V(Spread)              \
  V(LoadLocal)           \
  V(StoreLocal)          \
  V(WildcardPattern)     \
  V(LiteralPattern)      \
  V(IdentifierPattern)   \
  V(VariantPattern)      \
  V(RecordPattern)       \
  V(SeqPattern)          \
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
  Expr* children_[NumberOfChildren];

 protected:
  TemplateExpr() = default;

  void SetChildAt(const uint64_t idx, Expr* value) override {
    children_[idx] = value;
  }

 public:
  ~TemplateExpr() override = default;

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

class DynamicTemplateExpr : public Expr {
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

#define _HAS_NAMED_INPUT(Name, Type, Position)           \
  static constexpr const auto k##Name##Pos = (Position); \
                                                         \
 private:                                                \
  inline void Set##Name(Type* rhs) {                     \
    return SetChildAt(k##Name##Pos, rhs);                \
  }                                                      \
                                                         \
 public:                                                 \
  inline auto Get##Name() const -> Type* {               \
    return GetChildAt(k##Name##Pos);                     \
  }                                                      \
  inline auto Has##Name() const -> bool {                \
    return Get##Name() != nullptr;                       \
  }

#define HAS_NAMED_INPUT(Name, Position) _HAS_NAMED_INPUT(Name, Expr, Position)

#define HAS_NAMED_TYPED_INPUT(Name, Position)            \
  static constexpr const auto k##Name##Pos = (Position); \
                                                         \
 private:                                                \
  inline void Set##Name(Name##Expr* rhs) {               \
    return SetChildAt(k##Name##Pos, rhs);                \
  }                                                      \
                                                         \
 public:                                                 \
  inline auto Get##Name() const -> Name##Expr* {         \
    return GetChildAt(k##Name##Pos)->As##Name();         \
  }                                                      \
  inline auto Has##Name() const -> bool {                \
    return Get##Name() != nullptr;                       \
  }

class StoreLocalExpr : public TemplateExpr<1> {
 private:
  LocalVariable* local_;

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

  HAS_NAMED_INPUT(Value, 0);
  DECLARE_EXPR_TYPE(StoreLocal);

 public:
  static inline auto New(LocalVariable* local, Expr* value) -> StoreLocalExpr* {
    return new StoreLocalExpr(local, value);
  }
};

#define FOR_EACH_UNARY_OP(V) V(Negate)

enum UnaryOp {
#define DEFINE_OP(Name) k##Name,
  FOR_EACH_UNARY_OP(DEFINE_OP)
#undef DEFINE_OP
};

static inline constexpr auto ToString(const UnaryOp rhs) -> std::string_view {
  switch (rhs) {
#define DEFINE_TOSTRING(Name) \
  case UnaryOp::k##Name:      \
    return #Name;
    FOR_EACH_UNARY_OP(DEFINE_TOSTRING)
#undef DEFINE_TOSTRING
    default:
      return "Unknown";
  }
}

static inline auto operator<<(std::ostream& stream, const UnaryOp& rhs) -> std::ostream& {
  return stream << ToString(rhs);
}

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

#define FOR_EACH_BINARY_OP(V) \
  V(Add)                      \
  V(Subtract)                 \
  V(Multiply)                 \
  V(Divide)                   \
  V(Modulus)                  \
  V(Eq)                       \
  V(Neq)                      \
  V(GreaterThan)              \
  V(GreaterThanEqual)         \
  V(LessThan)                 \
  V(LessThanEqual)

enum BinaryOp {
#define DEFINE_BINARY_OP(Name) k##Name,
  FOR_EACH_BINARY_OP(DEFINE_BINARY_OP)
#undef DEFINE_BINARY_OP
};

static inline auto ToString(const BinaryOp rhs) -> std::string_view {
  switch (rhs) {
#define DEFINE_TOSTRING(Name) \
  case BinaryOp::k##Name:     \
    return #Name;
    FOR_EACH_BINARY_OP(DEFINE_TOSTRING)
#undef DEFINE_TOSTRING
    default:
      return "Unknown";
  }
}

static inline auto operator<<(std::ostream& stream, const BinaryOp rhs) -> std::ostream& {
  return stream << ToString(rhs);
}

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
    return GetOp() != BinaryOp::k##Name; \
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

class IdentifierPatternExpr : public PatternExpr {
 private:
  std::string name_{};

  inline void SetName(const std::string name) {
    name_ = std::move(name);
  }

 public:
  explicit IdentifierPatternExpr(const std::string name) {
    SetName(name);
  }
  ~IdentifierPatternExpr() override = default;

  // TODO(@s0cks): rename to GetName and replace Expr::GetName() with Expr::GetExprName()
  auto GetIdentifier() const -> const std::string& {
    return name_;
  }

  DECLARE_EXPR_TYPE(IdentifierPattern);

 public:
  static inline auto New(const std::string name) -> PatternExpr* {
    return new IdentifierPatternExpr(std::move(name));
  }
};

class RecordPatternExpr : public PatternExpr {
  using PropertyList = std::vector<std::string>;

 private:
  PropertyList properties_{};

 public:
  explicit RecordPatternExpr(const PropertyList properties) :
    properties_(std::move(properties)) {}
  ~RecordPatternExpr() override = default;

  auto GetProperties() const -> const PropertyList& {
    return properties_;
  }

  DECLARE_EXPR_TYPE(RecordPattern);
};

class VariantPatternExpr : public PatternExpr {
  // TODO(@s0cks): implement

  // /// Tag(p1, p2, …)
  // struct VariantPattern : Pattern {
  //   std::string tag;
  //   std::vector<PatternPtr> args;
  //   void accept(ASTVisitor&) override;
  // };
  DECLARE_EXPR_TYPE(VariantPattern);
};

class SeqPatternExpr : public PatternExpr {
  // TODO(@s0cks): implement

  // /// [ p1, p2, … ]
  // struct ListPattern : Pattern {
  //   std::vector<PatternPtr> elements;
  //   void accept(ASTVisitor&) override;
  // };
  DECLARE_EXPR_TYPE(SeqPattern);
};

struct Case {
  PatternExpr* pattern;
  Expr* body;
};

class CaseVisitor {
 protected:
  CaseVisitor() = default;

 public:
  virtual ~CaseVisitor() = default;
  virtual auto VisitCase(PatternExpr* pattern, Expr* body) -> bool = 0;
};

using CaseList = std::vector<Case>;

class MatchExpr : public Expr {
 private:
  Expr* subject_;
  CaseList cases_{};

  void SetCaseAt(const uint64_t idx, const Case value) {
    cases_[idx] = std::move(value);
  }

  inline void SetCaseAt(const uint64_t idx, PatternExpr* pattern, Expr* body) {
    return SetCaseAt(idx, {pattern, body});
  }

  inline void SetCasePatternAt(const uint64_t idx, PatternExpr* pattern) {
    cases_[idx].pattern = pattern;
  }

  inline void SetCaseBodyAt(const uint64_t idx, Expr* body) {
    cases_[idx].body = body;
  }

 public:
  explicit MatchExpr(Expr* subject, const CaseList cases) :
    subject_(subject),
    cases_(std::move(cases)) {}
  ~MatchExpr() override = default;

  auto GetSubject() const -> Expr* {
    return subject_;
  }

  inline auto HasSubject() const -> bool {
    return subject_;
  }

  auto GetCases() const -> const CaseList& {
    return cases_;
  }

  inline auto HasCases() const -> bool {
    return !cases_.empty();
  }

  auto GetNumberOfCases() const -> uint64_t {
    return cases_.size();
  }

  auto GetCaseAt(const uint64_t idx) const -> const Case& {
    return cases_.at(idx);
  }

  inline auto GetCasePatternAt(const uint64_t idx) const -> PatternExpr* {
    if (idx > cases_.size())
      return nullptr;
    return cases_[idx].pattern;
  }

  inline auto GetCaseBodyAt(const uint64_t idx) const -> Expr* {
    if (idx > cases_.size())
      return nullptr;
    return cases_[idx].body;
  }

  DECLARE_EXPR_TYPE(Match);

 public:
  static inline auto New(Expr* subject, const CaseList cases = {}) -> Expr* {
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
  std::string binding_{};

  inline void SetBinding(const std::string value) {
    binding_ = std::move(value);
  }

 public:
  ListComprehensionExpr(Expr* iterable, Expr* body, const std::string binding, Expr* clause = nullptr) {
    SetIterable(iterable);
    SetBinding(std::move(binding));
    SetBody(body);
    SetClause(clause);
  }
  ~ListComprehensionExpr() override = default;

  auto GetBinding() const -> const std::string& {
    return binding_;
  }

  HAS_NAMED_INPUT(Iterable, 0);
  HAS_NAMED_INPUT(Body, 1);
  HAS_NAMED_INPUT(Clause, 2);
  DECLARE_EXPR_TYPE(ListComprehension);
};

class NodeExpr : public DynamicTemplateExpr {
 public:
  enum Kind {
#define DEFINE_KIND(Name) k##Name,
    FOR_EACH_ELEMENT_NODE(DEFINE_KIND)
#undef DEFINE_KIND
  };

  static inline auto ToString(const Kind& rhs) -> std::string_view {
    switch (rhs) {
#define DEFINE_CHECK(Name) \
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
  elem::Property* properties_ = nullptr;

  inline void SetKind(const Kind rhs) {
    kind_ = rhs;
  }

  inline void SetProperties(elem::Property* rhs) {
    properties_ = rhs;
  }

  inline void AddProperty(elem::Property* p) {
    elem::Property::Append(&properties_, p);
  }

 public:
  explicit NodeExpr(const Kind kind, elem::Property* properties) :
    DynamicTemplateExpr(),
    kind_(kind),
    properties_(properties) {}
  ~NodeExpr() override = default;

  auto GetProperties() const -> elem::Property* {
    return properties_;
  }

  inline auto HasProperties() const -> bool {
    return GetProperties() != nullptr;
  }

  auto GetKind() const -> Kind {
    return kind_;
  }

#define DEFINE_TYPE_CHECK(Name)          \
  inline auto Is##Name() const -> bool { \
    return GetKind() == Kind::k##Name;   \
  }
  FOR_EACH_ELEMENT_NODE(DEFINE_TYPE_CHECK)
#undef DEFINE_TYPE_CHECK

  DECLARE_EXPR_TYPE(Node);

 public:
  static inline auto New(const Kind kind, elem::Property* properties = nullptr) -> NodeExpr* {
    return new NodeExpr(kind, properties);
  }

#define DEFINE_NEW(Name)                                                            \
  static inline auto New##Name(elem::Property* properties = nullptr) -> NodeExpr* { \
    return New(Kind::k##Name, properties);                                          \
  }
  FOR_EACH_ELEMENT_NODE(DEFINE_NEW)
#undef DEFINE_NEW
};

// // ─── Postfix parts ────────────────────────────────────────────────────────────
// struct FunctionCallPart {
//     std::vector<ExprPtr> args;
// };
//
// struct PropertyAccessPart {
//     std::string name;
//     bool        optional{false};   ///< true for ?.
// };
//
// struct IndexAccessPart {
//     ExprPtr index;
// };
//
// /// Discriminated union for one postfix suffix
// struct PostfixPart {
//     enum class Kind { Call, Prop, Index } kind;
//
//     // Only one of these is active, chosen by kind:
//     FunctionCallPart  call{};
//     PropertyAccessPart prop{};
//     IndexAccessPart   index{};
// };
//
// /// primary postfixPart*
// struct PostfixExpr : Expr {
//     ExprPtr                  primary;
//     std::vector<PostfixPart> parts;
//     void accept(ASTVisitor&) override;
// };

class RecordPropertyExpr : public TemplateExpr<1> {
 private:
  std::string name_;

  inline void SetName(const std::string name) {
    name_ = std::move(name);
  }

 public:
  explicit RecordPropertyExpr(const std::string name, Expr* value = nullptr) {
    SetName(std::move(name));
    SetValue(value);
  }
  ~RecordPropertyExpr() override = default;

  auto GetPropertyName() const -> const std::string& {
    return name_;
  }

  HAS_NAMED_INPUT(Value, 0);
  DECLARE_EXPR_TYPE(RecordProperty);

 public:
  static inline auto New(const std::string name, Expr* value) -> RecordPropertyExpr* {
    return new RecordPropertyExpr(std::move(name), value);
  }
};

class RecordExpr : public Expr {
 public:
  using SpreadList = std::vector<SpreadExpr*>;
  using PropertyList = std::vector<RecordPropertyExpr*>;

 private:
  SpreadList spreads_{};
  PropertyList properties_{};

  inline void SetSpreadAt(const uint64_t idx, SpreadExpr* value) {
    spreads_[idx] = value;
  }

  inline void SetPropertyAt(const uint64_t idx, RecordPropertyExpr* value) {
    properties_[idx] = value;
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

  auto GetNumberOfSpreads() const -> uint64_t {
    return spreads_.size();
  }

  auto GetSpreadAt(const uint64_t idx) const -> SpreadExpr* {
    return spreads_[idx];
  }

  auto HasSpreadAt(const uint64_t idx) const -> bool {
    return idx < spreads_.size() && spreads_[idx] != nullptr;
  }

  auto GetProperties() const -> const PropertyList& {
    return properties_;
  }

  auto GetNumberOfProperties() const -> uint64_t {
    return properties_.size();
  }

  auto GetPropertyAt(const uint64_t idx) const -> RecordPropertyExpr* {
    return properties_[idx];
  }

  auto HasPropertyAt(const uint64_t idx) const -> bool {
    return idx < properties_.size() && properties_[idx] != nullptr;
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
  auto VisitProperties(ExprVisitor* vis) -> bool;
  auto VisitProperties(const std::function<bool(Expr*)> vis) -> bool;
  auto VisitChildren(ExprVisitor* vis) -> VisitResult override;
  auto VisitChildren(const std::function<VisitResult(Expr*)> vis) -> VisitResult override;
  DECLARE_EXPR_TYPE(Record);

 public:
  static inline auto New(const SpreadList spreads, const PropertyList properties) -> Expr* {
    return new RecordExpr(std::move(spreads), std::move(properties));
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
    args_(std::move(args)) {}
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
      return GetTarget();
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
}  // namespace kura::expr

#endif  // KURA_EXPR_H
