#ifndef KURA_EXPR_BUILDER_H
#define KURA_EXPR_BUILDER_H

// clang-format off
#include "KuraLexer.h"
#include "KuraParser.h"
#include "KuraParserBaseVisitor.h"
// clang-format on
#include "expr.h"

namespace kura::expr {
class ExprBuilder : public KuraParserBaseVisitor {
 public:
  ExprBuilder() = default;
  ~ExprBuilder() override = default;

  auto visitSource(KuraParser::SourceContext* ctx) -> std::any override;

  // ╭──────────────╮
  // │ Declarations │
  // ╰──────────────╯
  auto visitImportDecl(KuraParser::ImportDeclContext* ctx) -> std::any override;
  auto visitTypeDecl(KuraParser::TypeDeclContext* ctx) -> std::any override;
  auto visitFuncDecl(KuraParser::FuncDeclContext* ctx) -> std::any override;

  // ╭─────────────╮
  // │ Expressions │
  // ╰─────────────╯
  auto visitLiteral(KuraParser::LiteralContext* ctx) -> std::any override;
  auto visitIfExpr(KuraParser::IfExprContext* ctx) -> std::any override;

  // ╭───────────────────╮
  // │ Match Expressions │
  // ╰───────────────────╯
  auto visitMatchExpr(KuraParser::MatchExprContext* ctx) -> std::any override;
  auto visitMatchArm(KuraParser::MatchArmContext* ctx) -> std::any override;
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
};
}  // namespace kura::expr

#endif  // KURA_EXPR_BUILDER_H
