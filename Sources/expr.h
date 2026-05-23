#ifndef KURA_EXPR_H
#define KURA_EXPR_H

#include <cstdint>
#include <memory>
#include <vector>

#include "common.h"
#include "type.h"

namespace kura::expr {
#define FOR_EACH_EXPR_TYPE(V) \
  V(Binding)                  \
  V(For)                      \
  V(If)                       \
  V(Invoke)                   \
  V(Literal)                  \
  V(BinaryOp)                 \
  V(UnaryOp)                  \
  V(Match)                    \
  V(SpreadArg)                \
  V(NamedArg)

struct Expr;
// clang-format off
#define DECLARE_EXPR(Name) \
  struct Name##Expr;
FOR_EACH_EXPR_TYPE(DECLARE_EXPR)
#undef DECLARE_EXPR

enum ExprType : uint8_t {
  kInvalidExpr = 0,
#define DEFINE_TYPE(Name) k##Name##Expr,
  FOR_EACH_EXPR_TYPE(DEFINE_TYPE)
#undef DEFINE_TYPE
  kTotalNumberOfExprNodes,
};
// clang-format on
using ExprPtr = std::shared_ptr<Expr>;
using ExprList = std::vector<ExprPtr>;

struct Expr {
  ExprType type;

  constexpr Expr(const ExprType t) :
    type(t) {}
  constexpr Expr(const Expr& rhs) = default;
  constexpr Expr(Expr&& rhs) = default;
  ~Expr() = default;

  auto operator=(const Expr& rhs) -> Expr& = default;
  auto operator=(Expr&& rhs) -> Expr& = default;
};

struct BindingExpr : Expr {
  constexpr BindingExpr() :
    Expr(kBindingExpr) {}
  ~BindingExpr() = default;

  DEFINE_DEFAULT_COPYABLE_TYPE(BindingExpr);
};

struct ForExpr : Expr {};

struct IfExpr : Expr {};

struct MatchExpr : Expr {};

struct UnaryOpExpr : Expr {
  ExprPtr value;

  constexpr UnaryOpExpr() :
    Expr(kUnaryOpExpr) {}
  constexpr UnaryOpExpr(const ExprPtr v) :
    Expr(kUnaryOpExpr),
    value(std::move(v)) {}
  ~UnaryOpExpr() = default;

  DEFINE_DEFAULT_COPYABLE_TYPE(UnaryOpExpr);
};

struct BinaryOpExpr : Expr {
  ExprPtr left{};
  ExprPtr right{};

  constexpr BinaryOpExpr() :
    Expr(kBinaryOpExpr) {}
  constexpr BinaryOpExpr(const ExprPtr l, const ExprPtr r) :
    Expr(kBinaryOpExpr),
    left(std::move(l)),
    right(std::move(r)) {}
  ~BinaryOpExpr() = default;

  DEFINE_DEFAULT_COPYABLE_TYPE(BinaryOpExpr);
};

struct InvokeExpr : Expr {
  ExprList args{};

  constexpr InvokeExpr() :
    Expr(kInvokeExpr) {}
  InvokeExpr(const ExprList a) :
    Expr(kInvokeExpr),
    args(std::move(a)) {}
  ~InvokeExpr() = default;

  DEFINE_DEFAULT_COPYABLE_TYPE(InvokeExpr);
};

struct LiteralExpr : Expr {
  kura::Value* value;

  constexpr LiteralExpr(kura::Value* v) :
    Expr(kLiteralExpr),
    value(v) {}
  constexpr LiteralExpr(const LiteralExpr& rhs) = default;
  constexpr LiteralExpr(LiteralExpr&& rhs) = default;
  ~LiteralExpr() = default;

  auto operator=(const LiteralExpr& rhs) -> LiteralExpr& = default;
  auto operator=(LiteralExpr&& rhs) -> LiteralExpr& = default;
};

struct SpreadArgExpr : Expr {
  ExprPtr value{};

  constexpr SpreadArgExpr() :
    Expr(kSpreadArgExpr) {}
  constexpr SpreadArgExpr(const ExprPtr v) :
    Expr(kSpreadArgExpr),
    value(std::move(v)) {}
  ~SpreadArgExpr() = default;

  DEFINE_DEFAULT_COPYABLE_TYPE(SpreadArgExpr);
};

struct NamedArgExpr : Expr {
  std::string name;
  ExprPtr value{};

  constexpr NamedArgExpr() :
    Expr(kNamedArgExpr) {}
  constexpr NamedArgExpr(const std::string n, const ExprPtr v) :
    Expr(kNamedArgExpr),
    name(std::move(n)),
    value(std::move(v)) {}
  ~NamedArgExpr() = default;

  DEFINE_DEFAULT_COPYABLE_TYPE(NamedArgExpr);
};
}  // namespace kura::expr

#endif  // KURA_EXPR_H
