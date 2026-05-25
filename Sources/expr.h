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
  V(UnaryOp)             \
  V(BinaryOp)            \
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
 public:
  explicit LiteralExpr() :
    Expr() {}
  ~LiteralExpr() override = default;

  DECLARE_EXPR_TYPE(Literal);
};

class UnaryOpExpr : public Expr {
 public:
  UnaryOpExpr() = default;
  ~UnaryOpExpr() override = default;

  DECLARE_EXPR_TYPE(UnaryOp);
};

class BinaryOpExpr : public Expr {
 public:
  BinaryOpExpr() = default;
  ~BinaryOpExpr() override = default;

  DECLARE_EXPR_TYPE(BinaryOp);
};

class MatchExpr : public Expr {
 public:
  MatchExpr() = default;
  ~MatchExpr() override = default;

  DECLARE_EXPR_TYPE(Match);
};
}  // namespace kura::expr

#endif  // KURA_EXPR_H
