#include "expr_builder.h"

namespace kura::expr {
auto ExprBuilder::visitSource(KuraParser::SourceContext* ctx) -> std::any {
  // TODO(@s0cks): get module name from parse tree
  auto m = Module::New("KuraModule");
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
  return nullptr;
}

auto ExprBuilder::visitTypeDecl(KuraParser::TypeDeclContext* ctx) -> std::any {
  return nullptr;
}

auto ExprBuilder::visitFuncDecl(KuraParser::FuncDeclContext* ctx) -> std::any {
  const auto name = ctx->IDENTIFIER()->getText();
  auto func = Function::New(name);
  // TODO(@s0cks): parse arguments into function def
  // const auto body = visit(ctx->expression());
  // if (body.has_value())
  //   func->SetBody(std::any_cast<Expr*>(body));
  return func;
}

auto ExprBuilder::visitIfExpr(KuraParser::IfExprContext* ctx) -> std::any {
  // IMPLEMENT
  return nullptr;
}

auto ExprBuilder::visitMatchExpr(KuraParser::MatchExprContext* ctx) -> std::any {
  // IMPLEMENT
  return nullptr;
}

auto ExprBuilder::visitMatchArm(KuraParser::MatchArmContext* ctx) -> std::any {
  // IMPLEMENT
  return nullptr;
}

auto ExprBuilder::visitPattern(KuraParser::PatternContext* ctx) -> std::any {
  // IMPLEMENT
  return nullptr;
}

auto ExprBuilder::visitWildcardPattern(KuraParser::WildcardPatternContext* ctx) -> std::any {
  // IMPLEMENT
  return nullptr;
}

auto ExprBuilder::visitLiteralPattern(KuraParser::LiteralPatternContext* ctx) -> std::any {
  // IMPLEMENT
  return nullptr;
}

auto ExprBuilder::visitIdentifierPattern(KuraParser::IdentifierPatternContext* ctx) -> std::any {
  // IMPLEMENT
  return nullptr;
}

auto ExprBuilder::visitVariantPattern(KuraParser::VariantPatternContext* ctx) -> std::any {
  // IMPLEMENT
  return nullptr;
}

auto ExprBuilder::visitRecordPattern(KuraParser::RecordPatternContext* ctx) -> std::any {
  // IMPLEMENT
  return nullptr;
}

auto ExprBuilder::visitPatternFieldList(KuraParser::PatternFieldListContext* ctx) -> std::any {
  // IMPLEMENT
  return nullptr;
}

auto ExprBuilder::visitPatternField(KuraParser::PatternFieldContext* ctx) -> std::any {
  // IMPLEMENT
  return nullptr;
}

auto ExprBuilder::visitListPattern(KuraParser::ListPatternContext* ctx) -> std::any {
  // IMPLEMENT
  return nullptr;
}

auto ExprBuilder::visitPatternList(KuraParser::PatternListContext* ctx) -> std::any {
  // IMPLEMENT
  return nullptr;
}

auto ExprBuilder::visitLiteral(KuraParser::LiteralContext* ctx) -> std::any {
  // IMPLEMENT
  return nullptr;
}
}  // namespace kura::expr
