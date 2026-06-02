#include "expr_builder.h"

#include "KuraParser.h"
#include "function.h"
#include "module.h"
#include "object.h"

namespace kura::expr {
auto ModuleBuilder::Build(KuraParser::SourceContext* ctx) -> Module* {
  expr::ExprBuilder builder(this);
  return std::any_cast<Module*>(builder(ctx));
}

auto ExprBuilder::CreateFunctionInScope(const std::string name) -> Function* {
  const auto func = Function::New(std::move(name));
  const auto local = GetScope()->CreateLocal(func->name);
  if (local)
    return func;

  std::stringstream ss{};
  ss << "failed to create local for function: " << func->ToString() << std::endl;
  throw std::runtime_error(ss.str());
}

auto ExprBuilder::visitSource(KuraParser::SourceContext* ctx) -> std::any {
  // TODO(@s0cks): get module name from parse tree
  const auto m = Module::New("Kura");
  // m->span = makeSpan(ctx);
  for (auto d : ctx->declaration()) {
    const auto decl = visit(d);
    if (!decl.has_value())
      continue;

    if (decl.type() == typeid(Function*)) {
      m->AddFunction(std::any_cast<Function*>(decl));
      continue;
    }
  }
  return m;
}

auto ExprBuilder::visitImportDecl(KuraParser::ImportDeclContext* ctx) -> std::any {
  std::cerr << __PRETTY_FUNCTION__ << " is not implemented!" << std::endl;
  return nullptr;
}

auto ExprBuilder::visitTypeDecl(KuraParser::TypeDeclContext* ctx) -> std::any {
  std::cerr << __PRETTY_FUNCTION__ << " is not implemented!" << std::endl;
  return nullptr;
}

auto ExprBuilder::visitFuncDecl(KuraParser::FuncDeclContext* ctx) -> std::any {
  const auto name = ctx->IDENTIFIER()->getText();
  const auto func = CreateFunctionInScope(std::move(name));
  PushScope();
  {
    const auto parameters = ctx->parameterList();
    if (parameters) {
      for (const auto& param : parameters->parameter()) {
        const auto ident = param->IDENTIFIER()->getText();
        const auto local = GetScope()->CreateLocal(ident);
        if (!local) {
          std::stringstream ss{};
          ss << "failed to create LocalVariable for parameter: " << ident;
          throw std::runtime_error(ss.str());
        }
      }
    }

    PushScope();
    {
      const auto body = visit(ctx->expression());
      if (body.has_value()) {
        auto body_expr = std::any_cast<expr::Expr*>(body);
        if (!body_expr->IsSeq())
          body_expr = SeqExpr::New(body_expr);
        func->body = body_expr;
      }
    }
    PopScope();
  }
  PopScope();
  return func;
}

auto ExprBuilder::visitRecordExpr(KuraParser::RecordExprContext* ctx) -> std::any {
  RecordExprBuilder builder(GetOwner());
  return builder(ctx);
}

auto ExprBuilder::visitBlockExpr(KuraParser::BlockExprContext* ctx) -> std::any {
  BlockExprBuilder builder(GetOwner());
  return builder.visitBlockExpr(ctx);
}

auto ExprBuilder::visitIfExpr(KuraParser::IfExprContext* ctx) -> std::any {
  const auto cond_expr = VisitExpr(ctx->expression());
  const auto then_expr = BlockExprToSeqExpr(ctx->blockExpr(0));
  const auto else_expr = BlockExprToSeqExpr(ctx->blockExpr(1));
  return IfExpr::New(cond_expr, then_expr, else_expr);
}

auto ExprBuilder::visitLiteralString(KuraParser::LiteralStringContext* ctx) -> std::any {
  const auto text = ctx->getText();
  return LiteralString(std::move(text));
}

auto ExprBuilder::visitLiteralNumber(KuraParser::LiteralNumberContext* ctx) -> std::any {
  const auto text = ctx->getText();
  const auto value = atof(text.data());
  return LiteralNumber(value);
}

auto ExprBuilder::visitLiteralNone(KuraParser::LiteralNoneContext* ctx) -> std::any {
  return LiteralNone();
}

auto ExprBuilder::visitLiteralTrue(KuraParser::LiteralTrueContext* ctx) -> std::any {
  return LiteralTrue();
}

auto ExprBuilder::visitLiteralFalse(KuraParser::LiteralFalseContext* ctx) -> std::any {
  return LiteralFalse();
}

auto ExprBuilder::visitLiteralMeasurement(KuraParser::LiteralMeasurementContext* ctx) -> std::any {
  // TODO(@s0cks): implement
  std::cerr << __PRETTY_FUNCTION__ << " is not implemented!" << std::endl;
  return nullptr;
}

auto ExprBuilder::visitBinaryOpExpr(KuraParser::BinaryOpExprContext* ctx) -> std::any {
  if (ctx->children.size() == 1)
    return visit(ctx->children.at(0));

  if (ctx->children.size() != 3) {
    std::cerr << "Error: Expected binary expression with 3 components." << std::endl;
    return nullptr;
  }

  auto lhs_any = visit(ctx->children.at(0));
  if (!lhs_any.has_value()) {
    std::cerr << "Error: Failed to process Left-Hand Side expression: " << ctx->children.at(0)->toString() << std::endl;
    return nullptr;
  }

  auto rhs_any = visit(ctx->children.at(2));
  if (!rhs_any.has_value()) {
    std::cerr << "Error: Failed to process Right-Hand Side expression." << std::endl;
    return nullptr;
  }

  std::string op_text = ctx->children.at(1)->getText();
  BinaryOp op;
  if (op_text == "+")
    op = BinaryOp::kAdd;
  else if (op_text == "-")
    op = BinaryOp::kSubtract;
  else if (op_text == "*")
    op = BinaryOp::kMultiply;
  else if (op_text == "/")
    op = BinaryOp::kDivide;
  else if (op_text == "%")
    op = BinaryOp::kModulus;
  else if (op_text == "==")
    op = BinaryOp::kEq;
  else if (op_text == "!=")
    op = BinaryOp::kNeq;
  else if (op_text == "<")
    op = BinaryOp::kLessThan;
  else if (op_text == "<=")
    op = BinaryOp::kLessThanEqual;
  else if (op_text == ">")
    op = BinaryOp::kGreaterThan;
  else if (op_text == ">=")
    op = BinaryOp::kGreaterThanEqual;
  // else if (op_text == "&&")
  //   op = BinaryOp::kAnd;
  // else if (op_text == "||")
  //   op = BinaryOp::kOr;
  // else if (op_text == "|")
  //   op = BinaryOp::kPipe;
  // else if (op_text == "=")
  //   op = BinaryOp::kAssign;
  else {
    std::cerr << "Error: Unknown binary operator: " << op_text << std::endl;
    return nullptr;
  }

  auto lhs_node = std::any_cast<Expr*>(lhs_any);
  auto rhs_node = std::any_cast<Expr*>(rhs_any);
  return BinaryExpr::New(op, lhs_node, rhs_node);
}

auto ExprBuilder::visitListExpr(KuraParser::ListExprContext* ctx) -> std::any {
  ListExprBuilder builder(GetOwner());
  return builder(ctx);
}

auto ExprBuilder::visitQualifiedName(KuraParser::QualifiedNameContext* ctx) -> std::any {
  const auto ident = ctx->IDENTIFIER(0)->getText();
  const auto local = GetScope()->GetLocalRecursive(ident);
  if (local)
    return LoadLocalExpr::New(local);

  std::cerr << "failed to find " << ident << std::endl;
  std::stringstream ss{};
  ss << "failed to find local named " << ident << " in scope.";
  throw std::runtime_error(ss.str());
}

auto ExprBuilder::visitUiExpr(KuraParser::UiExprContext* ctx) -> std::any {
  // TODO(@s0cks): implement
  const auto tag = ctx->IDENTIFIER()->getText();
  if (tag == "text")
    return NodeExpr::NewText();
  else if (tag == "button")
    return NodeExpr::NewButton();
  std::cerr << "invalid ui-expr tag: " << tag;
  return nullptr;
}

auto ExprBuilder::visitUiProps(KuraParser::UiPropsContext* ctx) -> std::any {
  // TODO(@s0cks): implement
  std::cerr << __PRETTY_FUNCTION__ << " is not implemented!" << std::endl;
  return nullptr;
}

auto ExprBuilder::visitUiPropList(KuraParser::UiPropListContext* ctx) -> std::any {
  // TODO(@s0cks): implement
  std::cerr << __PRETTY_FUNCTION__ << " is not implemented!" << std::endl;
  return nullptr;
}

auto ExprBuilder::visitUiProp(KuraParser::UiPropContext* ctx) -> std::any {
  // TODO(@s0cks): implement
  std::cerr << __PRETTY_FUNCTION__ << " is not implemented!" << std::endl;
  return nullptr;
}

auto ExprBuilder::visitUiChildren(KuraParser::UiChildrenContext* ctx) -> std::any {
  // TODO(@s0cks): implement
  std::cerr << __PRETTY_FUNCTION__ << " is not implemented!" << std::endl;
  return nullptr;
}

class ArgListBuilder : public ExprBuilder {
 private:
  ExprList results_{};

 public:
  explicit ArgListBuilder(ModuleBuilder* owner) :
    ExprBuilder(owner) {}
  ~ArgListBuilder() override = default;

  auto GetResults() const -> const ExprList& {
    return results_;
  }

  inline auto HasResults() const -> bool {
    return !results_.empty();
  }

  auto visitArgumentList(KuraParser::ArgumentListContext* ctx) -> std::any override {
    if (!ctx)
      goto finished;

    for (const auto& arg : ctx->expression()) {
      const auto value = VisitExpr(arg);
      if (!value)
        throw std::runtime_error("failed to visit argument");

      results_.push_back(value);
    }

  finished:
    return nullptr;
  }
};

auto ExprBuilder::visitPostfixExpr(KuraParser::PostfixExprContext* ctx) -> std::any {
  const auto target = VisitExpr(ctx->primaryExpr());
  if (!target)
    throw std::runtime_error("failed to visit target");

  for (const auto& part : ctx->postfixPart()) {
    if (part->functionCall()) {
      const auto call = part->functionCall();

      ArgListBuilder args(GetOwner());
      args.visitArgumentList(call->argumentList());
      return CallExpr::New(target, std::move(args.GetResults()));
    }
  }

  return visitChildren(ctx);
}

auto ExprBuilder::visitFunctionCall(KuraParser::FunctionCallContext* ctx) -> std::any {
  std::cerr << __PRETTY_FUNCTION__ << " is not implemented" << std::endl;  // TODO(@s0cks): implement
  return nullptr;
}

auto ExprBuilder::visitPropertyAccess(KuraParser::PropertyAccessContext* ctx) -> std::any {
  std::cerr << __PRETTY_FUNCTION__ << " is not implemented" << std::endl;  // TODO(@s0cks): implement
  return nullptr;
}

auto ExprBuilder::visitOptionalPropertyAccess(KuraParser::OptionalPropertyAccessContext* ctx) -> std::any {
  std::cerr << __PRETTY_FUNCTION__ << " is not implemented" << std::endl;  // TODO(@s0cks): implement
  return nullptr;
}

auto ExprBuilder::visitIndexAccess(KuraParser::IndexAccessContext* ctx) -> std::any {
  std::cerr << __PRETTY_FUNCTION__ << " is not implemented" << std::endl;  // TODO(@s0cks): implement
  return nullptr;
}

auto BlockExprBuilder::visitBlockExpr(KuraParser::BlockExprContext* ctx) -> std::any {
  for (const auto stmt : ctx->statement()) {
    const auto expr = VisitExpr(stmt);
    if (expr)
      expressions_.push_back(expr);
  }
  return SeqExpr::New(expressions_);
}

auto MatchExprBuilder::visitMatchExpr(KuraParser::MatchExprContext* ctx) -> std::any {
  const auto subject = VisitExpr(ctx->expression());
  if (!subject) {
    std::cerr << "failed to visit subject of match-expr" << std::endl;
    return nullptr;
  }

  for (const auto arm : ctx->matchArm()) {
    const auto pattern = VisitPatternExpr(arm->pattern());
    auto body = VisitExpr(arm->expression());
    if (!body->IsSeq())
      body = SeqExpr::New(body);
    cases_.push_back(Case{pattern, body});
  }

  return MatchExpr::New(subject, cases_);
}

auto MatchExprBuilder::visitPattern(KuraParser::PatternContext* ctx) -> std::any {
  // TODO(@s0cks): implement
  std::cerr << __PRETTY_FUNCTION__ << " is not implemented!" << std::endl;
  return nullptr;
}

auto MatchExprBuilder::visitWildcardPattern(KuraParser::WildcardPatternContext* ctx) -> std::any {
  // TODO(@s0cks): implement
  std::cerr << __PRETTY_FUNCTION__ << " is not implemented!" << std::endl;
  return nullptr;
}

auto MatchExprBuilder::visitLiteralPattern(KuraParser::LiteralPatternContext* ctx) -> std::any {
  // TODO(@s0cks): implement
  std::cerr << __PRETTY_FUNCTION__ << " is not implemented!" << std::endl;
  return nullptr;
}

auto MatchExprBuilder::visitIdentifierPattern(KuraParser::IdentifierPatternContext* ctx) -> std::any {
  // TODO(@s0cks): implement
  std::cerr << __PRETTY_FUNCTION__ << " is not implemented!" << std::endl;
  return nullptr;
}

auto MatchExprBuilder::visitVariantPattern(KuraParser::VariantPatternContext* ctx) -> std::any {
  // TODO(@s0cks): implement
  std::cerr << __PRETTY_FUNCTION__ << " is not implemented!" << std::endl;
  return nullptr;
}

auto MatchExprBuilder::visitRecordPattern(KuraParser::RecordPatternContext* ctx) -> std::any {
  // TODO(@s0cks): implement
  std::cerr << __PRETTY_FUNCTION__ << " is not implemented!" << std::endl;
  return nullptr;
}

auto MatchExprBuilder::visitPatternFieldList(KuraParser::PatternFieldListContext* ctx) -> std::any {
  // TODO(@s0cks): implement
  std::cerr << __PRETTY_FUNCTION__ << " is not implemented!" << std::endl;
  return nullptr;
}

auto MatchExprBuilder::visitPatternField(KuraParser::PatternFieldContext* ctx) -> std::any {
  // TODO(@s0cks): implement
  std::cerr << __PRETTY_FUNCTION__ << " is not implemented!" << std::endl;
  return nullptr;
}

auto MatchExprBuilder::visitListPattern(KuraParser::ListPatternContext* ctx) -> std::any {
  // TODO(@s0cks): implement
  std::cerr << __PRETTY_FUNCTION__ << " is not implemented!" << std::endl;
  return nullptr;
}

auto MatchExprBuilder::visitPatternList(KuraParser::PatternListContext* ctx) -> std::any {
  // TODO(@s0cks): implement
  std::cerr << __PRETTY_FUNCTION__ << " is not implemented!" << std::endl;
  return nullptr;
}

auto RecordExprBuilder::visitRecordExpr(KuraParser::RecordExprContext* ctx) -> std::any {
  if (!ctx->recordFieldList())
    return RecordExpr::New({}, {});

  const auto result = visit(ctx->recordFieldList());
  if (!result.has_value() || !std::any_cast<bool>(result)) {
    std::cerr << __PRETTY_FUNCTION__ << " is not implemented!" << std::endl;
    return nullptr;
  }

  return RecordExpr::New(std::move(spreads_), std::move(properties_));
}

auto RecordExprBuilder::visitRecordFieldList(KuraParser::RecordFieldListContext* ctx) -> std::any {
  for (const auto& field : ctx->recordField()) {
    const auto name = field->IDENTIFIER()->getText();
    const auto property = visit(field->expression());
    const auto body = std::any_cast<Expr*>(property);
    properties_.push_back(RecordPropertyExpr::New(name, body));
  }

  return true;
}

auto RecordExprBuilder::visitRecordField(KuraParser::RecordFieldContext* ctx) -> std::any {
  const auto name = ctx->IDENTIFIER()->getText();
  const auto value = visit(ctx->expression());
  if (!value.has_value() || !std::any_cast<bool>(value)) {
    std::cerr << "failed to get record property value" << std::endl;
    return nullptr;
  }

  return RecordPropertyExpr::New(std::move(name), std::any_cast<Expr*>(value));
}

auto ListExprBuilder::visitListExpr(KuraParser::ListExprContext* ctx) -> std::any {
  if (ctx->argumentList()) {
    for (const auto& expr : ctx->argumentList()->expression()) {
      const auto value = VisitExpr(expr);
      if (value)
        values_.push_back(std::any_cast<Expr*>(value));
    }
  }

  return ListExpr::New(values_);
}

auto ListExprBuilder::visitListComprehensionExpr(KuraParser::ListComprehensionExprContext* ctx) -> std::any {
  // TODO(@s0cks): implement
  std::cerr << __PRETTY_FUNCTION__ << " is not implemented!" << std::endl;
  return nullptr;
}

auto ListExprBuilder::visitListComprehensionClause(KuraParser::ListComprehensionClauseContext* ctx) -> std::any {
  // TODO(@s0cks): implement
  std::cerr << __PRETTY_FUNCTION__ << " is not implemented!" << std::endl;
  return nullptr;
}
}  // namespace kura::expr
