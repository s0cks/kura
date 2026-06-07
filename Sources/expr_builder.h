#ifndef KURA_EXPR_BUILDER_H
#define KURA_EXPR_BUILDER_H

// clang-format off
#include "KuraLexer.h"
#include "KuraParser.h"
#include "KuraParserBaseVisitor.h"
// clang-format on

#include "expr.h"
#include "function.h"
#include "local_scope.h"
#include "object.h"
#include "property.h"

namespace kura {
class Module;
}

namespace kura::expr {
class ModuleBuilder {
  friend class ExprBuilder;
  DEFINE_NON_COPYABLE_TYPE(ModuleBuilder);

 private:
  LocalScope* scope_;

  inline auto PushScope() -> LocalScope* {
    const auto new_scope = LocalScope::New(scope_);
    scope_ = new_scope;
    return new_scope;
  }

  inline auto PopScope() -> LocalScope* {
    if (scope_->IsRoot())
      return nullptr;
    const auto old_scope = scope_;
    scope_ = old_scope->GetParent();
    return old_scope;
  }

 public:
  explicit ModuleBuilder(LocalScope* scope) :
    scope_(scope) {}
  ~ModuleBuilder() = default;

  auto GetScope() const -> LocalScope* {
    return scope_;
  }

  auto Build(KuraParser::SourceContext* ctx) -> Module*;

  auto operator()(KuraParser::SourceContext* ctx) -> Module* {
    return Build(ctx);
  }
};

class TypeExprBuilder : public KuraParserBaseVisitor {
  DEFINE_NON_COPYABLE_TYPE(TypeExprBuilder);

 private:
  ModuleBuilder* owner_;
  Type* result_ = nullptr;

 public:
  explicit TypeExprBuilder(ModuleBuilder* owner) :
    owner_(owner) {}
  ~TypeExprBuilder() override = default;

  auto GetResult() const -> Type* {
    return result_;
  }

  inline auto HasResult() const -> bool {
    return GetResult() != nullptr;
  }

  auto visitTypeDecl(KuraParser::TypeDeclContext* ctx) -> std::any override;
  auto visitTypeVariantList(KuraParser::TypeVariantListContext* ctx) -> std::any override;
  auto visitTypeVariant(KuraParser::TypeVariantContext* ctx) -> std::any override;
};

class ExprBuilder : public KuraParserBaseVisitor {
 private:
  ModuleBuilder* owner_;

 protected:
  template <typename C, class E = Expr>
  inline auto VisitExpr(C* ctx) -> E* {
    if (!ctx)
      return nullptr;
    const auto value = visit(ctx);
    if (value.has_value())
      return std::any_cast<E*>(value);
    return nullptr;
  }

  inline auto BlockExprToSeqExpr(KuraParser::BlockExprContext* blk) -> SeqExpr* {
    if (blk == nullptr)
      return nullptr;
    const auto blk_value = visit(blk);
    if (!blk_value.has_value())
      return nullptr;
    return std::any_cast<expr::SeqExpr*>(blk_value);
  }

  inline auto PushScope() -> LocalScope* {
    return GetOwner()->PushScope();
  }

  inline auto PopScope() -> LocalScope* {
    return GetOwner()->PopScope();
  }

  inline auto CreateFunctionInScope(const std::string name) -> Function*;

  inline auto Literal(Object* rhs) -> Expr* {
    return LiteralExpr::New(rhs);
  }

  inline auto LiteralTrue() -> Expr* {
    return Literal(Bool::True());
  }

  inline auto LiteralFalse() -> Expr* {
    return Literal(Bool::False());
  }

  inline auto LiteralNone() -> Expr* {
    return Literal(None::Get());
  }

  inline auto LiteralString(const std::string rhs) -> Expr* {
    return Literal(String::New(std::move(rhs)));
  }

  inline auto LiteralString(const std::string_view rhs) -> Expr* {
    return LiteralString(std::string(rhs));
  }

  inline auto LiteralNumber(const double rhs) -> Expr* {
    return Literal(Number::New(rhs));
  }

 public:
  explicit ExprBuilder(ModuleBuilder* owner) :
    KuraParserBaseVisitor(),
    owner_(owner) {}
  ~ExprBuilder() override = default;

  auto GetOwner() const -> ModuleBuilder* {
    return owner_;
  }

  inline auto GetScope() const -> LocalScope* {
    return GetOwner()->GetScope();
  }

  auto visitSource(KuraParser::SourceContext* ctx) -> std::any override;
  auto visitBinaryOpExpr(KuraParser::BinaryOpExprContext* ctx) -> std::any override;
  auto visitUnaryExpr(KuraParser::UnaryExprContext* ctx) -> std::any override;
  auto visitQualifiedName(KuraParser::QualifiedNameContext* ctx) -> std::any override;
  auto visitBlockExpr(KuraParser::BlockExprContext* ctx) -> std::any override;
  auto visitRecordExpr(KuraParser::RecordExprContext* ctx) -> std::any override;
  auto visitListExpr(KuraParser::ListExprContext* ctx) -> std::any override;
  auto visitPrimaryExpr(KuraParser::PrimaryExprContext* ctx) -> std::any override;
  auto visitPostfixExpr(KuraParser::PostfixExprContext* ctx) -> std::any override;
  auto visitFunctionCall(KuraParser::FunctionCallContext* ctx) -> std::any override;
  auto visitPropertyAccess(KuraParser::PropertyAccessContext* ctx) -> std::any override;
  auto visitOptionalPropertyAccess(KuraParser::OptionalPropertyAccessContext* ctx) -> std::any override;
  auto visitIndexAccess(KuraParser::IndexAccessContext* ctx) -> std::any override;
  auto visitMatchExpr(KuraParser::MatchExprContext* ctx) -> std::any override;

  // ╭──────────────╮
  // │ Declarations │
  // ╰──────────────╯
  auto visitImportDecl(KuraParser::ImportDeclContext* ctx) -> std::any override;
  auto visitTypeDecl(KuraParser::TypeDeclContext* ctx) -> std::any override;
  auto visitFuncDecl(KuraParser::FuncDeclContext* ctx) -> std::any override;

  // ╭─────────────╮
  // │ Expressions │
  // ╰─────────────╯
  auto visitIfExpr(KuraParser::IfExprContext* ctx) -> std::any override;
  auto visitLiteralString(KuraParser::LiteralStringContext* ctx) -> std::any override;
  auto visitLiteralNumber(KuraParser::LiteralNumberContext* ctx) -> std::any override;
  auto visitLiteralNone(KuraParser::LiteralNoneContext* ctx) -> std::any override;
  auto visitLiteralTrue(KuraParser::LiteralTrueContext* ctx) -> std::any override;
  auto visitLiteralFalse(KuraParser::LiteralFalseContext* ctx) -> std::any override;
  auto visitLiteralMeasurement(KuraParser::LiteralMeasurementContext* ctx) -> std::any override;

  // ╭────╮
  // │ UI │
  // ╰────╯
  auto visitUiExpr(KuraParser::UiExprContext* ctx) -> std::any override;
  auto visitUiProps(KuraParser::UiPropsContext* ctx) -> std::any override;
  auto visitUiPropList(KuraParser::UiPropListContext* ctx) -> std::any override;
  auto visitUiProp(KuraParser::UiPropContext* ctx) -> std::any override;
  auto visitUiChildren(KuraParser::UiChildrenContext* ctx) -> std::any override;

  auto operator()(KuraParser::SourceContext* ctx) -> std::any {
    return visit(ctx);
  }
};

class BlockExprBuilder : public ExprBuilder {
 private:
  ExprList expressions_{};

 public:
  explicit BlockExprBuilder(ModuleBuilder* owner) :
    ExprBuilder(owner) {}
  ~BlockExprBuilder() override = default;

  auto visitBlockExpr(KuraParser::BlockExprContext* ctx) -> std::any override;
};

class RecordExprBuilder : public ExprBuilder {
 private:
  std::vector<RecordPropertyExpr*> properties_{};
  std::vector<SpreadExpr*> spreads_{};

 public:
  explicit RecordExprBuilder(ModuleBuilder* owner) :
    ExprBuilder(owner) {}
  ~RecordExprBuilder() override = default;

  auto visitRecordExpr(KuraParser::RecordExprContext* ctx) -> std::any override;
  auto visitRecordFieldList(KuraParser::RecordFieldListContext* ctx) -> std::any override;
  auto visitRecordField(KuraParser::RecordFieldContext* ctx) -> std::any override;

  auto operator()(KuraParser::RecordExprContext* ctx) -> std::any {
    return visitRecordExpr(ctx);
  }
};

class MatchExprBuilder : public ExprBuilder {
 private:
  CaseList cases_{};

  template <typename C, class E = PatternExpr>
  inline auto VisitPatternExpr(C* ctx) -> E* {
    return VisitExpr<C, E>(ctx);
  }

 public:
  explicit MatchExprBuilder(ModuleBuilder* owner) :
    ExprBuilder(owner) {}
  ~MatchExprBuilder() override = default;

  auto visitMatchExpr(KuraParser::MatchExprContext* ctx) -> std::any override;
  auto visitPattern(KuraParser::PatternContext* ctx) -> std::any override;
  auto visitWildcardPattern(KuraParser::WildcardPatternContext* ctx) -> std::any override;
  auto visitLiteralPattern(KuraParser::LiteralPatternContext* ctx) -> std::any override;
  auto visitIdentifierPattern(KuraParser::IdentifierPatternContext* ctx) -> std::any override;
  auto visitVariantPattern(KuraParser::VariantPatternContext* ctx) -> std::any override;
  auto visitRecordPattern(KuraParser::RecordPatternContext* ctx) -> std::any override;
  auto visitPatternFieldList(KuraParser::PatternFieldListContext* ctx) -> std::any override;
  auto visitPatternField(KuraParser::PatternFieldContext* ctx) -> std::any override;
  auto visitListPattern(KuraParser::ListPatternContext* ctx) -> std::any override;
  auto visitPatternList(KuraParser::PatternListContext* ctx) -> std::any override;

  auto operator()(KuraParser::MatchExprContext* ctx) -> std::any {
    return visitMatchExpr(ctx);
  }
};

class ExprListBuilder : public ExprBuilder {
 private:
  ExprList results_{};

 protected:
  void AddResults(const ExprList results) {
    results_.insert(std::end(results_), std::begin(results), std::end(results));
  }

  void AddResult(Expr* expr) {
    results_.push_back(expr);
  }

 public:
  explicit ExprListBuilder(ModuleBuilder* owner, const ExprList results = {}) :
    ExprBuilder(owner),
    results_(std::move(results)) {}
  ~ExprListBuilder() override = default;

  auto GetResults() const -> const ExprList& {
    return results_;
  }

  auto HasResults() const -> bool {
    return !results_.empty();
  }
};

class ArgListBuilder : public ExprListBuilder {
 public:
  ArgListBuilder(ModuleBuilder* owner, const ExprList results = {}) :
    ExprListBuilder(owner, std::move(results)) {}
  ~ArgListBuilder() override = default;

  auto visitArgumentList(KuraParser::ArgumentListContext* ctx) -> std::any override;
};

class ListExprBuilder : public ExprBuilder {
 public:
  explicit ListExprBuilder(ModuleBuilder* owner) :
    ExprBuilder(owner) {}
  ~ListExprBuilder() override = default;

  auto visitListExpr(KuraParser::ListExprContext* ctx) -> std::any override;
  auto visitListComprehensionExpr(KuraParser::ListComprehensionExprContext* ctx) -> std::any override;
  auto visitListComprehensionClause(KuraParser::ListComprehensionClauseContext* ctx) -> std::any override;

  auto operator()(KuraParser::ListExprContext* ctx) -> std::any {
    return visitListExpr(ctx);
  }
};
}  // namespace kura::expr

#endif  // KURA_EXPR_BUILDER_H
