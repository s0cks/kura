#ifndef KURA_EXPR_H
#define KURA_EXPR_H

#include <cstdint>
#include <memory>
#include <vector>

#include "common.h"
#include "type.h"

namespace kura::expr {
#define FOR_EACH_EXPR(V) \
  V(Literal)             \
  V(Unary)               \
  V(Binary)              \
  V(Seq)                 \
  V(If)                  \
  V(Match)               \
  V(Element)

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
  virtual auto Visit##Name(Name##Expr*) -> bool = 0;
  FOR_EACH_EXPR(DEFINE_VISIT)
#undef DEFINE_VISIT
  // clang-format on
};

#define DECLARE_EXPR_TYPE(Name)                       \
 public:                                              \
  auto GetName() const -> std::string_view override { \
    return #Name;                                     \
  }                                                   \
  auto Accept(ExprVisitor* vis) -> bool override;     \
  auto As##Name() -> Name##Expr* override {           \
    return this;                                      \
  }

class Expr {
 protected:
  Expr() = default;

 public:
  virtual ~Expr() = default;

  virtual auto GetName() const -> std::string_view = 0;
  virtual auto Accept(ExprVisitor* vis) -> bool = 0;
  virtual auto VisitChildren(const std::function<bool(Expr*)> vis) -> bool;
  virtual auto VisitChildren(ExprVisitor* vis) -> bool;
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

class SeqExpr : public Expr {
 private:
  ExprList children_{};

  void Append(Expr* child) {
    children_.push_back(child);
  }

 public:
  explicit SeqExpr(const ExprList children = {}) :
    Expr(),
    children_(children) {}
  ~SeqExpr() override = default;

  auto GetChildren() const -> const ExprList& {
    return children_;
  }

  inline auto HasChildren() const -> bool {
    return !children_.empty();
  }

  auto GetNumberOfChildren() -> const size_t {
    return children_.size();
  }

  auto GetChildAt(const size_t idx) const -> Expr* {
    return children_[idx];
  }

  inline auto HasChildAt(const size_t idx) const -> bool {
    return idx <= children_.size() && children_[idx] != nullptr;
  }

  auto VisitChildren(ExprVisitor* vis) -> bool override;
  auto VisitChildren(const std::function<bool(Expr*)> vis) -> bool override;
  DECLARE_EXPR_TYPE(Seq);

 public:
  static inline auto New(const ExprList children = {}) -> Expr* {
    return new SeqExpr(std::move(children));
  }
};

class LiteralExpr : public Expr {
 private:
  Type* value_;

 public:
  explicit LiteralExpr(Type* value) :
    Expr(),
    value_(value) {}
  ~LiteralExpr() override = default;

  auto GetValue() const -> Type* {
    return value_;
  }

  DECLARE_EXPR_TYPE(Literal);

 public:
  static inline auto New(Type* value) -> Expr* {
    return new LiteralExpr(value);
  }
};

#define FOR_EACH_UNARY_OP(V) V(Negate)

class UnaryExpr : public Expr {
 public:
  enum UnaryOp {
#define DEFINE_OP(Name) k##Name,
    FOR_EACH_UNARY_OP(DEFINE_OP)
#undef DEFINE_OP
  };

 private:
  UnaryOp op_;
  Expr* value_;

 public:
  UnaryExpr(const UnaryOp op, Expr* value) :
    op_(op),
    value_(value) {}
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

  auto GetValue() const -> Expr* {
    return value_;
  }

  auto VisitChildren(ExprVisitor* vis) -> bool override;
  auto VisitChildren(const std::function<bool(Expr*)> vis) -> bool override;
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
  V(Divide)

class BinaryExpr : public Expr {
 public:
  enum BinaryOp {
#define DEFINE_BINARY_OP(Name) k##Name,
    FOR_EACH_BINARY_OP(DEFINE_BINARY_OP)
#undef DEFINE_BINARY_OP
  };

 private:
  BinaryOp op_;
  Expr* left_;
  Expr* right_;

 public:
  BinaryExpr(const BinaryOp op, Expr* left, Expr* right) :
    op_(op),
    left_(left),
    right_(right) {}
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

  auto GetLeft() const -> Expr* {
    return left_;
  }

  auto GetRight() const -> Expr* {
    return right_;
  }

  auto VisitChildren(ExprVisitor* vis) -> bool override;
  auto VisitChildren(const std::function<bool(Expr*)> vis) -> bool override;
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

class IfExpr : public Expr {
 private:
  Expr* condition_;
  Expr* then_;
  Expr* else_;

 public:
  IfExpr(Expr* condition, Expr* then_expr, Expr* else_expr) :
    Expr(),
    condition_(condition),
    then_(then_expr),
    else_(else_expr) {}
  ~IfExpr() override = default;

  auto GetConditionExpr() const -> Expr* {
    return condition_;
  }

  auto GetThenExpr() const -> Expr* {
    return then_;
  }

  auto GetElseExpr() const -> Expr* {
    return else_;
  }

  inline auto HasElseExpr() const -> bool {
    return else_ != nullptr;
  }

  auto VisitChildren(ExprVisitor* vis) -> bool override;
  auto VisitChildren(const std::function<bool(Expr*)> vis) -> bool override;
  DECLARE_EXPR_TYPE(If);

 public:
  static inline auto New(Expr* condition, Expr* then_expr, Expr* else_expr = nullptr) -> Expr* {
    return new IfExpr(condition, then_expr, else_expr);
  }
};

#define FOR_EACH_PATTERN(V) \
  V(Literal)                \
  V(Default)

class Pattern;
#define DECLARE_PATTERN(Name) class Name##Pattern;
FOR_EACH_PATTERN(DECLARE_PATTERN)
#undef DECLARE_PATTERN

class PatternVisitor {
 protected:
  PatternVisitor() = default;

 public:
  virtual ~PatternVisitor() = default;
#define DEFINE_VISIT(Name) virtual auto Visit##Name(Name##Pattern* pattern) -> bool = 0;
  FOR_EACH_PATTERN(DEFINE_VISIT);
#undef DEFINE_VISIT
};

class Pattern {
 protected:
  Pattern() = default;

 public:
  virtual ~Pattern() = default;

  virtual auto Accept(PatternVisitor* vis) -> bool = 0;
  virtual auto GetPatternName() const -> std::string_view = 0;
#define DEFINE_PATTERN_TYPE_CHECK(Name)       \
  virtual auto As##Name() -> Name##Pattern* { \
    return nullptr;                           \
  }                                           \
  inline auto Is##Name() -> bool {            \
    return As##Name() != nullptr;             \
  }
  FOR_EACH_PATTERN(DEFINE_PATTERN_TYPE_CHECK)
#undef DEFINE_PATTERN_TYPE_CHECK
};

#define DECLARE_PATTERN_TYPE(Name)                           \
 public:                                                     \
  auto Accept(PatternVisitor* vis) -> bool override;         \
  auto GetPatternName() const -> std::string_view override { \
    return #Name;                                            \
  }                                                          \
  auto As##Name() -> Name##Pattern* override {               \
    return this;                                             \
  }

class LiteralPattern : public Pattern {
 public:
  LiteralPattern() = default;
  ~LiteralPattern() override = default;

  DECLARE_PATTERN_TYPE(Literal);

 public:
  static inline auto New() -> LiteralPattern* {
    return new LiteralPattern();
  }
};

class DefaultPattern : public Pattern {
 public:
  DefaultPattern() = default;
  ~DefaultPattern() override = default;

  DECLARE_PATTERN_TYPE(Default);

 public:
  static inline auto New() -> DefaultPattern* {
    return new DefaultPattern();
  }
};

using PatternList = std::vector<Pattern*>;

class MatchExpr : public Expr {
 private:
  PatternList patterns_{};

 public:
  explicit MatchExpr(const PatternList patterns) :
    Expr(),
    patterns_(std::move(patterns)) {}
  ~MatchExpr() override = default;

  auto GetPatterns() const -> const PatternList& {
    return patterns_;
  }

  auto GetNumberOfPatterns() const -> size_t {
    return patterns_.size();
  }

  auto HasPatterns() const -> bool {
    return !patterns_.empty();
  }

  auto GetPatternAt(const size_t idx) const -> Pattern* {
    return patterns_[idx];
  }

  auto HasPatternAt(const size_t idx) const -> bool {
    return idx < patterns_.size() && patterns_[idx] != nullptr;
  }

  auto HasDefaultPattern() const -> bool {
    for (const auto& pattern : patterns_) {
      if (pattern->IsDefault())
        return true;
    }
    return false;
  }

  auto VisitPatterns(PatternVisitor* vis) -> bool;
  auto VisitPatterns(const std::function<bool(Pattern*)> vis) -> bool;
  DECLARE_EXPR_TYPE(Match);

 public:
  static inline auto New(const PatternList patterns = {}) -> MatchExpr* {
    return new MatchExpr(std::move(patterns));
  }
};

class ElementExpr : public Expr {
 public:
  ElementExpr() = default;
  ~ElementExpr() override = default;

  DECLARE_EXPR_TYPE(Element);

 public:
  static inline auto New() -> Expr* {
    return new ElementExpr();
  }
};
}  // namespace kura::expr

#endif  // KURA_EXPR_H
