#ifndef KURA_EXPR_H
#define KURA_EXPR_H

#include "type.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace kura::expr {
#define FOR_EACH_EXPR_TYPE(V)                                                  \
  V(Binding)                                                                   \
  V(For)                                                                       \
  V(If)                                                                        \
  V(Invoke)                                                                    \
  V(Literal)                                                                   \
  V(BinaryOp)                                                                  \
  V(UnaryOp)                                                                   \
  V(Match)                                                                     \
  V(SpreadArg)                                                                 \
  V(NamedArg)

struct Expr;
// clang-format off
#define DECLARE_EXPR(Name) \
  struct Name##Expr;
FOR_EACH_EXPR_TYPE(DECLARE_EXPR)
#undef DECLARE_EXPR
// clang-format on

enum ExprType : uint8_t {
  // clang-format off
  kInvalidExpr = 0,
#define DEFINE_TYPE(Name) k##Name##Expr,
  FOR_EACH_EXPR_TYPE(DEFINE_TYPE)
#undef DEFINE_TYPE
  kTotalNumberOfExprNodes,
  // clang-format on
};
using ExprPtr = std::shared_ptr<Expr>;
using ExprList = std::vector<ExprPtr>;

struct Expr {
  ExprType type;

  constexpr Expr(const ExprType t) : type(t) {}
  constexpr Expr(const Expr &rhs) = default;
  constexpr Expr(Expr &&rhs) = default;
  ~Expr() = default;

  auto operator=(const Expr &rhs) -> Expr & = default;
  auto operator=(Expr &&rhs) -> Expr & = default;
};

struct BindingExpr : Expr {
  constexpr BindingExpr() : Expr(kBindingExpr) {}
  constexpr BindingExpr(const BindingExpr &rhs) = default;
  constexpr BindingExpr(BindingExpr &&rhs) = default;
  ~BindingExpr() = default;

  auto operator=(const BindingExpr &rhs) -> BindingExpr & = default;
  auto operator=(BindingExpr &&rhs) -> BindingExpr & = default;
};

struct ForExpr : Expr {};

struct IfExpr : Expr {};

struct MatchExpr : Expr {};

struct UnaryOpExpr : Expr {
  ExprPtr value;

  constexpr UnaryOpExpr() : Expr(kUnaryOpExpr) {}
  constexpr UnaryOpExpr(const ExprPtr v)
      : Expr(kUnaryOpExpr), value(std::move(v)) {}
  constexpr UnaryOpExpr(const UnaryOpExpr &rhs) = default;
  constexpr UnaryOpExpr(UnaryOpExpr &&rhs) = default;
  ~UnaryOpExpr() = default;

  auto operator=(const UnaryOpExpr &rhs) -> UnaryOpExpr & = default;
  auto operator=(UnaryOpExpr &&rhs) -> UnaryOpExpr & = default;
};

struct BinaryOpExpr : Expr {
  ExprPtr left{};
  ExprPtr right{};

  constexpr BinaryOpExpr() : Expr(kBinaryOpExpr) {}
  constexpr BinaryOpExpr(const ExprPtr l, const ExprPtr r)
      : Expr(kBinaryOpExpr), left(std::move(l)), right(std::move(r)) {}
  constexpr BinaryOpExpr(const BinaryOpExpr &rhs) = default;
  constexpr BinaryOpExpr(BinaryOpExpr &&rhs) = default;
  ~BinaryOpExpr() = default;

  auto operator=(const BinaryOpExpr &rhs) -> BinaryOpExpr & = default;
  auto operator=(BinaryOpExpr &&rhs) -> BinaryOpExpr & = default;
};

struct InvokeExpr : Expr {
  ExprList args{};

  constexpr InvokeExpr() : Expr(kInvokeExpr) {}
  InvokeExpr(const ExprList a) : Expr(kInvokeExpr), args(std::move(a)) {}
  constexpr InvokeExpr(const InvokeExpr &rhs) = default;
  constexpr InvokeExpr(InvokeExpr &&rhs) = default;
  ~InvokeExpr() = default;

  auto operator=(const InvokeExpr &rhs) -> InvokeExpr & = default;
  auto operator=(InvokeExpr &&rhs) -> InvokeExpr & = default;
};

struct LiteralExpr : Expr {
  kura::Value *value;

  constexpr LiteralExpr(kura::Value *v) : Expr(kLiteralExpr), value(v) {}
  constexpr LiteralExpr(const LiteralExpr &rhs) = default;
  constexpr LiteralExpr(LiteralExpr &&rhs) = default;
  ~LiteralExpr() = default;

  auto operator=(const LiteralExpr &rhs) -> LiteralExpr & = default;
  auto operator=(LiteralExpr &&rhs) -> LiteralExpr & = default;
};

struct SpreadArgExpr : Expr {
  ExprPtr value{};

  constexpr SpreadArgExpr() : Expr(kSpreadArgExpr) {}
  constexpr SpreadArgExpr(const ExprPtr v)
      : Expr(kSpreadArgExpr), value(std::move(v)) {}
  constexpr SpreadArgExpr(const SpreadArgExpr &rhs) = default;
  constexpr SpreadArgExpr(SpreadArgExpr &&rhs) = default;
  ~SpreadArgExpr() = default;

  auto operator=(const SpreadArgExpr &rhs) -> SpreadArgExpr & = default;
  auto operator=(SpreadArgExpr &&rhs) -> SpreadArgExpr & = default;
};

struct NamedArgExpr : Expr {
  std::string name;
  ExprPtr value{};

  constexpr NamedArgExpr() : Expr(kNamedArgExpr) {}
  constexpr NamedArgExpr(const std::string n, const ExprPtr v)
      : Expr(kNamedArgExpr), name(std::move(n)), value(std::move(v)) {}
  constexpr NamedArgExpr(const NamedArgExpr &rhs) = default;
  constexpr NamedArgExpr(NamedArgExpr &&rhs) = default;
  ~NamedArgExpr() = default;

  auto operator=(const NamedArgExpr &rhs) -> NamedArgExpr & = default;
  auto operator=(NamedArgExpr &&rhs) -> NamedArgExpr & = default;
};
} // namespace kura::expr

#endif // KURA_EXPR_H
