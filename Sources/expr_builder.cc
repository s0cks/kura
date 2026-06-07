#include "expr_builder.h"

#include <optional>

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
  const auto local = GetScope()->CreateLocal(func->GetName());
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

auto TypeExprBuilder::visitTypeDecl(KuraParser::TypeDeclContext* ctx) -> std::any {
  NOT_IMPLEMENTED;  // TODO(@s0cks): implement
  return visitChildren(ctx);
}

auto TypeExprBuilder::visitTypeVariantList(KuraParser::TypeVariantListContext* ctx) -> std::any {
  NOT_IMPLEMENTED;  // TODO(@s0cks): implement
  return visitChildren(ctx);
}

auto TypeExprBuilder::visitTypeVariant(KuraParser::TypeVariantContext* ctx) -> std::any {
  NOT_IMPLEMENTED;  // TODO(@s0cks): implement
  return visitChildren(ctx);
}

auto ExprBuilder::visitTypeDecl(KuraParser::TypeDeclContext* ctx) -> std::any {
  TypeExprBuilder builder(GetOwner());
  return builder.visitTypeDecl(ctx);
}

auto ExprBuilder::visitFuncDecl(KuraParser::FuncDeclContext* ctx) -> std::any {
  const auto name = ctx->IDENTIFIER()->getText();
  const auto func = CreateFunctionInScope(std::move(name));
  LocalsScope func_scope(GetOwner());
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

  {
    LocalsScope body_scope(GetOwner());
    const auto body = visit(ctx->expression());
    if (body.has_value()) {
      auto body_expr = std::any_cast<expr::Expr*>(body);
      if (!body_expr->IsSeq())
        body_expr = SeqExpr::New(body_expr);
      func->SetBody(body_expr->AsSeq());
    }
  }
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

#define FOR_EACH_BINARY_OP_TOKEN(V) \
  V(Add, "+")                       \
  V(Subtract, "-")                  \
  V(Multiply, "*")                  \
  V(Divide, "/")                    \
  V(Modulus, "%")                   \
  V(Eq, "==")                       \
  V(Neq, "!=")                      \
  V(LessThan, "<")                  \
  V(LessThanEqual, "<=")            \
  V(GreaterThan, ">")               \
  V(GreaterThanEqual, ">=")         \
  V(BinaryAnd, "&&")                \
  V(BinaryOr, "||")                 \
  V(Pipe, "|")

static inline auto GetBinaryOpFromText(const std::string& rhs) -> std::optional<BinaryOp> {
#define DEFINE_CASE(Name, Token) \
  if (rhs == (Token))            \
    return {BinaryOp::k##Name};

  FOR_EACH_BINARY_OP_TOKEN(DEFINE_CASE)
#undef DEFINE_CASE

  return std::nullopt;
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

  const auto op_token = ctx->children.at(1)->getText();
  const auto op = GetBinaryOpFromText(op_token);
  if (!op) {
    std::stringstream ss{};
    ss << "invalid BinaryOp token: " << op_token;
    throw std::runtime_error(ss.str());
  }

  auto lhs_node = std::any_cast<Expr*>(lhs_any);
  auto rhs_node = std::any_cast<Expr*>(rhs_any);
  return BinaryExpr::New(*op, lhs_node, rhs_node);
}

#define FOR_EACH_UNARY_OP_TOKEN(V) \
  V(Plus, "+")                     \
  V(Minus, "-")                    \
  V(Bang, "!")

static inline auto GetUnaryOpFromToken(const std::string& rhs) -> std::optional<UnaryOp> {
#define DEFINE_CASE(Name, Token) \
  if (rhs == (Token))            \
    return {UnaryOp::k##Name};

  FOR_EACH_UNARY_OP_TOKEN(DEFINE_CASE)
#undef DEFINE_CASE
  return std::nullopt;
}

auto ExprBuilder::visitUnaryExpr(KuraParser::UnaryExprContext* ctx) -> std::any {
  const auto& children = ctx->children;
  if (children.size() == 1)
    return visitChildren(ctx);

  const auto op_token = children.at(0)->getText();
  const auto op = GetUnaryOpFromToken(op_token);
  if (!op) {
    std::stringstream ss{};
    ss << "invalid UnaryOp token: " << op_token;
    throw std::runtime_error(ss.str());
  }

  const auto value = VisitExpr(ctx->children.at(1));
  if (!value) {
    std::stringstream ss{};
    ss << "failed to visit unary-expr child: " << children.at(1)->getText();
    throw std::runtime_error(ss.str());
  }

  return UnaryExpr::New(*op, value);
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

auto ArgListBuilder::visitArgumentList(KuraParser::ArgumentListContext* ctx) -> std::any {
  if (ctx) {
    const auto expressions = ctx->expression();
    if (expressions.empty())
      goto finished;

    ExprList results{};
    results.reserve(expressions.size());
    for (const auto& arg : expressions) {
      const auto value = VisitExpr(arg);
      if (!value)
        throw std::runtime_error("failed to visit argument");
      results.push_back(value);
    }

    AddResults(std::move(results));
  }

finished:
  return nullptr;
}

auto ExprBuilder::visitPrimaryExpr(KuraParser::PrimaryExprContext* ctx) -> std::any {
  if (ctx->IDENTIFIER()) {
    const auto ident = ctx->IDENTIFIER()->getText();

    LocalVariable* local = GetScope()->GetLocalRecursive(ident);
    if (!local) {
      std::stringstream ss{};
      ss << "failed to find LocalVariable: " << ident;
      throw std::runtime_error(ss.str());
    }

    return LoadLocalExpr::New(local);
  }

  return visitChildren(ctx);
}

auto ExprBuilder::visitPostfixExpr(KuraParser::PostfixExprContext* ctx) -> std::any {
  const auto target = VisitExpr(ctx->primaryExpr());
  if (!target)
    return visitChildren(ctx);

  const auto parts = ctx->postfixPart();
  for (const auto& part : parts) {
    if (part->functionCall()) {
      const auto call = part->functionCall();

      ArgListBuilder args(GetOwner());
      args.visitArgumentList(call->argumentList());
      return CallExpr::New(target, std::move(args.GetResults()));
    } else if (part->propertyAccess()) {
      const auto property = visit(part->propertyAccess());
      if (!property.has_value()) {
        std::stringstream ss{};
        ss << "invalid property: " << part->optionalPropertyAccess()->IDENTIFIER()->getText();
        throw std::runtime_error(ss.str());
      }

      return GetPropertyExpr::New(target, std::any_cast<Property*>(property));
    } else if (part->optionalPropertyAccess()) {
      const auto property = visit(part->optionalPropertyAccess());
      if (!property.has_value()) {
        std::stringstream ss{};
        ss << "invalid property: " << part->optionalPropertyAccess()->IDENTIFIER()->getText();
        throw std::runtime_error(ss.str());
      }

      return GetPropertyExpr::New(target, std::any_cast<Property*>(property), true);
    }
  }

  return target;
}

auto ExprBuilder::visitFunctionCall(KuraParser::FunctionCallContext* ctx) -> std::any {
  std::cerr << __PRETTY_FUNCTION__ << " is not implemented" << std::endl;  // TODO(@s0cks): implement
  return nullptr;
}

auto ExprBuilder::visitPropertyAccess(KuraParser::PropertyAccessContext* ctx) -> std::any {
  const auto name = ctx->IDENTIFIER()->getText();
  return Property::FindOrCreate(String::New(name), Type::NoneType());
}

auto ExprBuilder::visitOptionalPropertyAccess(KuraParser::OptionalPropertyAccessContext* ctx) -> std::any {
  const auto name = ctx->IDENTIFIER()->getText();
  return Property::FindOrCreate(String::New(name), Type::NoneType());
}

auto ExprBuilder::visitIndexAccess(KuraParser::IndexAccessContext* ctx) -> std::any {
  std::cerr << __PRETTY_FUNCTION__ << " is not implemented" << std::endl;  // TODO(@s0cks): implement
  return nullptr;
}

auto ExprBuilder::visitMatchExpr(KuraParser::MatchExprContext* ctx) -> std::any {
  MatchExprBuilder builder(GetOwner());
  return builder.visitMatchExpr(ctx);
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

  const auto expr = MatchExpr::New(subject)->AsMatch();
  for (const auto& arm : ctx->matchArm()) {
    const auto pattern = VisitPatternExpr(arm->pattern());

    auto body = VisitExpr(arm->expression());
    if (!body) {
      std::cerr << "failed to visit match arm body for: " << pattern->GetName()
                << std::endl;  /// TODO(@s0cks): use ToString() when its implement
      continue;
    }
    if (!body->IsSeq())
      body = SeqExpr::New(body);

    if (pattern->IsWildcardPattern()) {
      if (expr->HasWildcard())
        throw std::runtime_error("match expr already has wildcard expr");

      expr->SetWildcard(body);  // TODO(@s0cks): dont overwrite existing wildcards
      continue;
    }

    expr->AddCase({pattern, body});
  }

  return (Expr*)expr;
}

auto MatchExprBuilder::visitPattern(KuraParser::PatternContext* ctx) -> std::any {
  if (ctx->wildcardPattern())
    return visitWildcardPattern(ctx->wildcardPattern());
  else if (ctx->literalPattern())
    return visitLiteralPattern(ctx->literalPattern());

  throw std::runtime_error("invalid match pattern");
}

auto MatchExprBuilder::visitWildcardPattern(KuraParser::WildcardPatternContext* ctx) -> std::any {
  return WildcardPatternExpr::New();
}

auto MatchExprBuilder::visitLiteralPattern(KuraParser::LiteralPatternContext* ctx) -> std::any {
  const auto literal = VisitExpr(ctx->literal());
  return literal ? LiteralPatternExpr::New(literal->AsLiteral()) : nullptr;
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
    const auto property = visit(field);
    if (!property.has_value()) {
#ifdef KURA_DEBUG
      std::cerr << "failed to construct propery for record field" << std::endl;
#endif  // KURA_DEBUG
      continue;
    }

    properties_.push_back(std::any_cast<expr::StorePropertyExpr*>(property));
  }

  return true;
}

auto RecordExprBuilder::visitRecordField(KuraParser::RecordFieldContext* ctx) -> std::any {
  const auto name = ctx->IDENTIFIER()->getText();
  const auto property = Property::FindOrCreate(String::New(name), Type::NoneType());

  const auto value = visit(ctx->expression());
  if (!value.has_value())
    return nullptr;
  return StorePropertyExpr::New(property, std::any_cast<Expr*>(value));
}

auto ListExprBuilder::visitListExpr(KuraParser::ListExprContext* ctx) -> std::any {
  ArgListBuilder builder(GetOwner());
  builder.visitArgumentList(ctx->argumentList());
  return ListExpr::New(builder.GetResults());
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
