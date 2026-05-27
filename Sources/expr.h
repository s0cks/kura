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
  V(Match)

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

class LiteralExpr : public Expr {
 private:
  Value* value_;

 public:
  explicit LiteralExpr(Value* value) :
    Expr(),
    value_(value) {}
  ~LiteralExpr() override = default;

  auto GetValue() const -> Value* {
    return value_;
  }

  DECLARE_EXPR_TYPE(Literal);
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

class MatchExpr : public Expr {
 public:
  MatchExpr() = default;
  ~MatchExpr() override = default;

  DECLARE_EXPR_TYPE(Match);
};
}  // namespace kura::expr

#endif  // KURA_EXPR_H
