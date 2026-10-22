#ifndef KURA_EXPR_BUILDER_H
#define KURA_EXPR_BUILDER_H

#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

// clang-format off
#include "KuraLexer.h"
#include "KuraParser.h"
#include "KuraParserBaseVisitor.h"
// clang-format on

#include "frontend/expr.h"
#include "function.h"
#include "local_scope.h"
#include "object.h"
#include "property.h"

namespace kura {
struct Module;
}

namespace kura::expr {
// Raised when the parse tree is syntactically valid but cannot be turned into
// an Expr tree: either the program is wrong (undefined name, ...) or it uses
// syntax the Expr IR has no node for yet (see `Unsupported`).
class BuildError : public std::runtime_error {
 private:
  size_t line_;
  size_t column_;

 public:
  BuildError(const size_t line, const size_t column, const std::string& message) :
    std::runtime_error(message),
    line_(line),
    column_(column) {}
  ~BuildError() override = default;

  auto GetLine() const -> size_t {
    return line_;
  }

  auto GetColumn() const -> size_t {
    return column_;
  }

  auto ToString() const -> std::string {
    return std::to_string(line_) + ":" + std::to_string(column_) + ": " + what();
  }
};

class ExprBuilder;
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

  // Throws BuildError.
  auto Build(KuraParser::SourceContext* ctx) -> Module*;

  auto operator()(KuraParser::SourceContext* ctx) -> Module* {
    return Build(ctx);
  }
};

// Turns the v1 parse tree into Expr nodes.
//
// Every visit method that produces an expression returns an `Expr*` inside the
// std::any (never a subclass pointer), so callers can always `any_cast<Expr*>`.
class ExprBuilder : public KuraParserBaseVisitor {
  friend class UIExprBuilder;

 protected:
  class LocalsScope {
   private:
    ModuleBuilder* owner_;

   public:
    explicit LocalsScope(ModuleBuilder* owner) :
      owner_(owner) {
      GetOwner()->PushScope();
    }
    ~LocalsScope() {
      GetOwner()->PopScope();
    }

    auto GetOwner() const -> ModuleBuilder* {
      return owner_;
    }
  };

 private:
  ModuleBuilder* owner_;
  LocalScope* module_scope_ = nullptr;
  std::unordered_set<std::string> function_names_{};

  [[noreturn]] static void Error(antlr4::ParserRuleContext* ctx, const std::string& message);
  // Syntax that parses (it is part of v1) but that the Expr IR cannot represent yet.
  // Nothing in the core language is left in this state; it is only used for the
  // remaining unspecified corners (see the callers).
  [[noreturn]] static void Unsupported(antlr4::ParserRuleContext* ctx, const std::string& what);

  auto Build(antlr4::tree::ParseTree* tree) -> Expr*;
  auto BuildSeq(antlr4::tree::ParseTree* tree) -> SeqExpr*;
  auto BuildBlock(antlr4::ParserRuleContext* ctx, const std::vector<KuraParser::StatementContext*>& statements,
                  KuraParser::ExpressionContext* result) -> SeqExpr*;
  auto BuildStatement(KuraParser::StatementContext* ctx) -> Expr*;
  auto BuildBinding(const std::vector<KuraParser::DirectiveContext*>& directives, KuraParser::LetBindingContext* let,
                    KuraParser::RedefinitionContext* redef) -> StoreLocalExpr*;
  auto BuildDirectives(const std::vector<KuraParser::DirectiveContext*>& directives) -> DirectiveList;
  auto BuildType(KuraParser::TypeExprContext* ctx) -> TypeExpr*;
  auto BuildTypeMember(KuraParser::TypeMemberContext* ctx) -> TypeExpr*;
  auto BuildTypeDecl(KuraParser::TypeDeclContext* ctx, const std::vector<KuraParser::DirectiveContext*>& directives)
      -> TypeDecl*;
  auto BuildUnit(KuraParser::UnitLiteralContext* ctx, const bool negate) -> Object*;
  auto BuildCall(antlr4::ParserRuleContext* ctx, Expr* target, ExprList args) -> Expr*;
  auto BuildPipeTarget(KuraParser::PipeTargetContext* ctx, Expr* lhs) -> Expr*;
  auto BuildPattern(KuraParser::PatternContext* ctx) -> PatternExpr*;
  auto BuildLiteralPattern(KuraParser::LiteralPatternContext* ctx) -> PatternExpr*;
  auto BuildArgs(KuraParser::ArgumentListContext* ctx) -> ExprList;
  auto LoadName(antlr4::ParserRuleContext* ctx, const std::string& name) -> Expr*;
  auto ToSeq(Expr* expr) -> SeqExpr*;

  // `let x = ...` / `x = ...`. Redefinition inside the same block reuses the
  // local; redefinition from a nested block shadows it for that block only.
  auto DeclareLocal(antlr4::ParserRuleContext* ctx, const std::string& name) -> LocalVariable*;
  auto CreateLocal(antlr4::ParserRuleContext* ctx, const std::string& name) -> LocalVariable*;

  inline auto Literal(Object* rhs) -> Expr* {
    return LiteralExpr::New(rhs);
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

  // ╭──────────────╮
  // │ Declarations │
  // ╰──────────────╯
  auto visitSource(KuraParser::SourceContext* ctx) -> std::any override;
  auto visitFnDecl(KuraParser::FnDeclContext* ctx) -> std::any override;

  // ╭─────────────╮
  // │ Expressions │
  // ╰─────────────╯
  auto visitExpression(KuraParser::ExpressionContext* ctx) -> std::any override;
  auto visitPropertyAccess(KuraParser::PropertyAccessContext* ctx) -> std::any override;
  auto visitCallExpr(KuraParser::CallExprContext* ctx) -> std::any override;
  auto visitUnaryExpr(KuraParser::UnaryExprContext* ctx) -> std::any override;
  auto visitMultiplicativeExpr(KuraParser::MultiplicativeExprContext* ctx) -> std::any override;
  auto visitAdditiveExpr(KuraParser::AdditiveExprContext* ctx) -> std::any override;
  auto visitComparisonExpr(KuraParser::ComparisonExprContext* ctx) -> std::any override;
  auto visitEqualityExpr(KuraParser::EqualityExprContext* ctx) -> std::any override;
  auto visitLogicalAndExpr(KuraParser::LogicalAndExprContext* ctx) -> std::any override;
  auto visitLogicalOrExpr(KuraParser::LogicalOrExprContext* ctx) -> std::any override;
  auto visitPrimaryExpr(KuraParser::PrimaryExprContext* ctx) -> std::any override;

  // ╭──────────╮
  // │ Primary  │
  // ╰──────────╯
  auto visitNumberLit(KuraParser::NumberLitContext* ctx) -> std::any override;
  auto visitStringLit(KuraParser::StringLitContext* ctx) -> std::any override;
  auto visitTrueLit(KuraParser::TrueLitContext* ctx) -> std::any override;
  auto visitFalseLit(KuraParser::FalseLitContext* ctx) -> std::any override;
  auto visitNoneLit(KuraParser::NoneLitContext* ctx) -> std::any override;
  auto visitUnitLit(KuraParser::UnitLitContext* ctx) -> std::any override;
  auto visitVarRef(KuraParser::VarRefContext* ctx) -> std::any override;
  auto visitVariantRef(KuraParser::VariantRefContext* ctx) -> std::any override;
  auto visitSeqLit(KuraParser::SeqLitContext* ctx) -> std::any override;
  auto visitRecordLit(KuraParser::RecordLitContext* ctx) -> std::any override;
  auto visitBlockLit(KuraParser::BlockLitContext* ctx) -> std::any override;
  auto visitIfLit(KuraParser::IfLitContext* ctx) -> std::any override;
  auto visitMatchLit(KuraParser::MatchLitContext* ctx) -> std::any override;
  auto visitForLit(KuraParser::ForLitContext* ctx) -> std::any override;
  auto visitLambdaLit(KuraParser::LambdaLitContext* ctx) -> std::any override;
  auto visitUiLit(KuraParser::UiLitContext* ctx) -> std::any override;
  auto visitParenExpr(KuraParser::ParenExprContext* ctx) -> std::any override;

  auto visitRecordLiteral(KuraParser::RecordLiteralContext* ctx) -> std::any override;
  auto visitBlockExpr(KuraParser::BlockExprContext* ctx) -> std::any override;
  auto visitBlock(KuraParser::BlockContext* ctx) -> std::any override;
  auto visitIfExpr(KuraParser::IfExprContext* ctx) -> std::any override;
  auto visitMatchExpr(KuraParser::MatchExprContext* ctx) -> std::any override;
  auto visitForExpr(KuraParser::ForExprContext* ctx) -> std::any override;

  auto operator()(KuraParser::SourceContext* ctx) -> std::any {
    return visit(ctx);
  }
};

// Builds `#tag(args) { children }` into a NodeExpr. Directives on an element
// are hints and are currently dropped, as the Directives page allows.
class UIExprBuilder {
 private:
  ExprBuilder* parent_;

  auto BuildChildren(NodeExpr* node, KuraParser::UiChildrenContext* ctx) -> void;

 public:
  explicit UIExprBuilder(ExprBuilder* parent) :
    parent_(parent) {}
  ~UIExprBuilder() = default;

  auto Build(KuraParser::UiExprContext* ctx) -> Expr*;
};
}  // namespace kura::expr

#endif  // KURA_EXPR_BUILDER_H
