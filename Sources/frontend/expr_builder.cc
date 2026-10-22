#include "frontend/expr_builder.h"

#include <optional>
#include <sstream>
#include <unordered_map>

#include "KuraParser.h"
#include "function.h"
#include "module.h"
#include "object.h"

namespace kura::expr {
// ╭────────╮
// │ Errors │
// ╰────────╯
void ExprBuilder::Error(antlr4::ParserRuleContext* ctx, const std::string& message) {
  const auto token = ctx ? ctx->getStart() : nullptr;
  throw BuildError(token ? token->getLine() : 0, token ? token->getCharPositionInLine() : 0, message);
}

void ExprBuilder::Unsupported(antlr4::ParserRuleContext* ctx, const std::string& what) {
  Error(ctx, what + " is valid v1 syntax but the Expr IR has no node for it yet");
}

// ╭─────────╮
// │ Helpers │
// ╰─────────╯
auto ExprBuilder::Build(antlr4::tree::ParseTree* tree) -> Expr* {
  const auto value = visit(tree);
  if (!value.has_value())
    Error(dynamic_cast<antlr4::ParserRuleContext*>(tree), "failed to build expression");
  return std::any_cast<Expr*>(value);
}

auto ExprBuilder::ToSeq(Expr* expr) -> SeqExpr* {
  if (expr->IsSeq())
    return expr->AsSeq();
  return SeqExpr::New(expr);
}

auto ExprBuilder::BuildSeq(antlr4::tree::ParseTree* tree) -> SeqExpr* {
  return ToSeq(Build(tree));
}

auto ExprBuilder::LoadName(antlr4::ParserRuleContext* ctx, const std::string& name) -> Expr* {
  const auto local = GetScope()->GetLocalRecursive(name);
  if (!local)
    Error(ctx, "undefined name '" + name + "'");
  return LoadLocalExpr::New(local);
}

auto ExprBuilder::CreateLocal(antlr4::ParserRuleContext* ctx, const std::string& name) -> LocalVariable* {
  const auto local = GetScope()->CreateLocal(name);
  if (!local)
    Error(ctx, "'" + name + "' is already defined in this scope");
  return local;
}

auto ExprBuilder::DeclareLocal(antlr4::ParserRuleContext* ctx, const std::string& name) -> LocalVariable* {
  if (GetScope() == module_scope_ && function_names_.contains(name))
    Error(ctx, "'" + name + "' is already defined as a function");
  const auto existing = GetScope()->GetLocal(name);
  if (existing)
    return existing;
  return CreateLocal(ctx, name);
}

auto ExprBuilder::BuildArgs(KuraParser::ArgumentListContext* ctx) -> ExprList {
  ExprList args{};
  if (!ctx)
    return args;
  for (const auto arg : ctx->expression())
    args.push_back(Build(arg));
  return args;
}

auto ExprBuilder::BuildCall(antlr4::ParserRuleContext* ctx, Expr* target, ExprList args) -> Expr* {
  (void)ctx;
  return CallExpr::New(target, std::move(args));
}

// Strips the quotes and resolves \n \t \" \\ (the only escapes in v1).
static inline auto UnescapeString(const std::string& text) -> std::string {
  std::string result{};
  result.reserve(text.size());
  for (size_t i = 1; i + 1 < text.size(); i++) {
    if (text[i] != '\\') {
      result.push_back(text[i]);
      continue;
    }
    switch (text[++i]) {
      case 'n':
        result.push_back('\n');
        break;
      case 't':
        result.push_back('\t');
        break;
      case '"':
        result.push_back('"');
        break;
      case '\\':
        result.push_back('\\');
        break;
    }
  }
  return result;
}

// ╭──────────────╮
// │ Declarations │
// ╰──────────────╯
auto ModuleBuilder::Build(KuraParser::SourceContext* ctx) -> Module* {
  ExprBuilder builder(this);
  return std::any_cast<Module*>(builder(ctx));
}

auto ExprBuilder::visitSource(KuraParser::SourceContext* ctx) -> std::any {
  // TODO(@s0cks): get module name from parse tree
  const auto m = Module::New("Kura");
  module_scope_ = GetScope();

  // Pass 1: declare every function so calls may refer to functions declared
  // later in the file. (Recursion is rejected later, by the call-graph check.)
  struct Pending {
    KuraParser::ItemContext* item;
    KuraParser::FnDeclContext* decl;
    Function* func;
  };
  std::vector<Pending> pending{};
  for (const auto item : ctx->item()) {
    const auto decl = item->fnDecl();
    if (!decl)
      continue;
    const auto name = decl->IDENT()->getText();
    if (function_names_.contains(name))
      Error(decl, "function '" + name + "' is already defined");
    const auto func = Function::New(name);
    if (!GetScope()->CreateLocal(func->GetName()))
      Error(decl, "failed to declare function '" + name + "'");
    function_names_.insert(name);
    pending.push_back({item, decl, func});
  }

  // Pass 2: type declarations and top-level bindings, in source order. They are
  // built before any function body so bodies can use every top-level name.
  for (const auto item : ctx->item()) {
    if (item->typeDecl())
      m->AddType(BuildTypeDecl(item->typeDecl(), item->directive()));
    else if (item->letBinding() || item->redefinition())
      m->AddBinding(BuildBinding(item->directive(), item->letBinding(), item->redefinition()));
  }

  // Pass 3: function bodies.
  for (const auto& [item, decl, func] : pending) {
    LocalsScope func_scope(GetOwner());
    Function::ParameterList parameters{};
    if (const auto params = decl->paramList()) {
      for (const auto param : params->param()) {
        const auto local = CreateLocal(param, param->IDENT()->getText());
        parameters.push_back({local, BuildType(param->typeExpr())});
      }
    }
    func->SetParameters(std::move(parameters));
    if (decl->typeExpr())
      func->SetReturnType(BuildType(decl->typeExpr()));
    func->SetDirectives(BuildDirectives(item->directive()));

    LocalsScope body_scope(GetOwner());
    func->SetBody(BuildSeq(decl->expression()));
    m->AddFunction(func);
  }
  return m;
}

auto ExprBuilder::visitFnDecl(KuraParser::FnDeclContext* ctx) -> std::any {
  // Functions are built by visitSource (three passes). A FnDecl is never
  // visited on its own.
  Error(ctx, "unexpected function declaration");
}

// ╭────────────╮
// │ Directives │
// ╰────────────╯
// Directives are hints (wiki: Language-Directives); they are recorded on the
// node they annotate and may be ignored by every later pass.
auto ExprBuilder::BuildDirectives(const std::vector<KuraParser::DirectiveContext*>& directives) -> DirectiveList {
  DirectiveList result{};
  for (const auto d : directives) {
    const auto directive = new Directive();
    directive->name = d->DIRECTIVE()->getText().substr(1);  // strip '@'
    if (const auto list = d->uiArgList()) {
      for (const auto arg : list->uiArg()) {
        std::string name = "value";
        KuraParser::ExpressionContext* value = nullptr;
        if (const auto named = dynamic_cast<KuraParser::NamedUiArgContext*>(arg)) {
          name = named->IDENT()->getText();
          value = named->expression();
        } else if (const auto positional = dynamic_cast<KuraParser::PositionalUiArgContext*>(arg)) {
          value = positional->expression();
        } else {
          Error(arg, "spread is not allowed in directive arguments");
        }
        const auto property = Property::FindOrCreate(String::New(name), Type::NoneType());
        directive->args.push_back(StorePropertyExpr::New(property, Build(value)));
      }
    }
    result.push_back(directive);
  }
  return result;
}

// ╭───────╮
// │ Types │
// ╰───────╯
auto ExprBuilder::BuildType(KuraParser::TypeExprContext* ctx) -> TypeExpr* {
  if (!ctx)
    return nullptr;
  std::vector<TypeExpr*> members{};
  for (const auto member : ctx->typeMember())
    members.push_back(BuildTypeMember(member));
  if (members.size() == 1)
    return members.front();
  return new UnionTypeExpr(std::move(members));
}

auto ExprBuilder::BuildTypeMember(KuraParser::TypeMemberContext* ctx) -> TypeExpr* {
  if (const auto variant = dynamic_cast<KuraParser::VariantTypeContext*>(ctx))
    return new VariantTypeExpr(variant->UPPER_IDENT()->getText(), BuildType(variant->typeExpr()));

  if (const auto named = dynamic_cast<KuraParser::NamedTypeContext*>(ctx)) {
    std::vector<TypeExpr*> args{};
    if (const auto type_args = named->typeArgs()) {
      for (const auto arg : type_args->typeExpr())
        args.push_back(BuildType(arg));
    }
    return new NamedTypeExpr(named->UPPER_IDENT()->getText(), std::move(args));
  }

  if (dynamic_cast<KuraParser::NoneTypeContext*>(ctx))
    return new NamedTypeExpr("None");

  if (const auto record = dynamic_cast<KuraParser::RecordTypeMemberContext*>(ctx)) {
    std::vector<RecordTypeExpr::Field> fields{};
    for (const auto field : record->recordType()->recordField())
      fields.push_back({field->IDENT()->getText(), BuildType(field->typeExpr()), field->QMARK() != nullptr});
    return new RecordTypeExpr(std::move(fields));
  }

  if (const auto function = dynamic_cast<KuraParser::FunctionTypeMemberContext*>(ctx)) {
    // `(A, B) => R`: every typeExpr but the last is a parameter.
    auto types = function->functionType()->typeExpr();
    const auto result = BuildType(types.back());
    types.pop_back();
    std::vector<TypeExpr*> params{};
    for (const auto param : types)
      params.push_back(BuildType(param));
    return new FunctionTypeExpr(std::move(params), result);
  }

  Error(ctx, "invalid type");
}

auto ExprBuilder::BuildTypeDecl(KuraParser::TypeDeclContext* ctx,
                                const std::vector<KuraParser::DirectiveContext*>& directives) -> TypeDecl* {
  std::vector<std::string> params{};
  if (const auto type_params = ctx->typeParams()) {
    for (const auto param : type_params->UPPER_IDENT())
      params.push_back(param->getText());
  }
  return new TypeDecl(ctx->UPPER_IDENT()->getText(), std::move(params), BuildType(ctx->typeExpr()),
                      BuildDirectives(directives));
}

// ╭─────────────╮
// │ Expressions │
// ╰─────────────╯
auto ExprBuilder::visitExpression(KuraParser::ExpressionContext* ctx) -> std::any {
  auto result = Build(ctx->operation());
  // `x |> f(y)` is `f(x, y)`; the grammar guarantees the right side is a call.
  for (const auto target : ctx->pipeTarget())
    result = BuildPipeTarget(target, result);
  return result;
}

auto ExprBuilder::BuildPipeTarget(KuraParser::PipeTargetContext* ctx, Expr* lhs) -> Expr* {
  const auto callee = ctx->pipeCallee();
  Expr* target = nullptr;
  if (callee->UPPER_IDENT()) {
    // `payload |> Click()` is `Click(payload)`.
    if (ctx->argumentList() && !ctx->argumentList()->expression().empty())
      Error(ctx, "a piped variant takes no further arguments");
    return VariantExpr::New(callee->UPPER_IDENT()->getText(), lhs);
  } else if (callee->expression()) {
    target = Build(callee->expression());
  } else {
    const auto names = callee->IDENT();
    target = LoadName(callee, names.at(0)->getText());
    for (size_t i = 1; i < names.size(); i++) {
      const auto property = Property::FindOrCreate(String::New(names.at(i)->getText()), Type::NoneType());
      target = GetPropertyExpr::New(target, property);
    }
  }

  ExprList args{lhs};
  for (const auto arg : BuildArgs(ctx->argumentList()))
    args.push_back(arg);
  return BuildCall(ctx, target, std::move(args));
}

auto ExprBuilder::visitPropertyAccess(KuraParser::PropertyAccessContext* ctx) -> std::any {
  const auto target = Build(ctx->operation());
  const auto property = Property::FindOrCreate(String::New(ctx->IDENT()->getText()), Type::NoneType());
  return GetPropertyExpr::New(target, property);
}

auto ExprBuilder::visitCallExpr(KuraParser::CallExprContext* ctx) -> std::any {
  // `Click(payload)` constructs a variant rather than calling a function.
  if (const auto primary = dynamic_cast<KuraParser::PrimaryExprContext*>(ctx->operation())) {
    if (const auto variant = dynamic_cast<KuraParser::VariantRefContext*>(primary->primary())) {
      const auto name = variant->UPPER_IDENT()->getText();
      const auto args = BuildArgs(ctx->argumentList());
      if (args.size() != 1)
        Error(ctx, "variant '" + name + "' takes exactly one payload");
      return VariantExpr::New(name, args.front());
    }
  }

  const auto target = Build(ctx->operation());
  return BuildCall(ctx, target, BuildArgs(ctx->argumentList()));
}

auto ExprBuilder::visitUnaryExpr(KuraParser::UnaryExprContext* ctx) -> std::any {
  const auto op = ctx->op->getType() == KuraLexer::MINUS ? UnaryOp::kMinus : UnaryOp::kBang;
  return UnaryExpr::New(op, Build(ctx->operation()));
}

#define DEFINE_BINARY_VISIT(Ctx, Op)                                         \
  return BinaryExpr::New((Op), Build(ctx->operation(0)), Build(ctx->operation(1)))

auto ExprBuilder::visitMultiplicativeExpr(KuraParser::MultiplicativeExprContext* ctx) -> std::any {
  DEFINE_BINARY_VISIT(Multiplicative, ctx->op->getType() == KuraLexer::STAR ? BinaryOp::kMultiply : BinaryOp::kDivide);
}

auto ExprBuilder::visitAdditiveExpr(KuraParser::AdditiveExprContext* ctx) -> std::any {
  // BIN_MINUS is `-` with whitespace on both sides (see KuraLexer.g4).
  DEFINE_BINARY_VISIT(Additive, ctx->op->getType() == KuraLexer::PLUS ? BinaryOp::kAdd : BinaryOp::kSubtract);
}

auto ExprBuilder::visitComparisonExpr(KuraParser::ComparisonExprContext* ctx) -> std::any {
  BinaryOp op;
  switch (ctx->op->getType()) {
    case KuraLexer::LT:
      op = BinaryOp::kLessThan;
      break;
    case KuraLexer::LE:
      op = BinaryOp::kLessThanEqual;
      break;
    case KuraLexer::GT:
      op = BinaryOp::kGreaterThan;
      break;
    default:
      op = BinaryOp::kGreaterThanEqual;
      break;
  }
  DEFINE_BINARY_VISIT(Comparison, op);
}

auto ExprBuilder::visitEqualityExpr(KuraParser::EqualityExprContext* ctx) -> std::any {
  DEFINE_BINARY_VISIT(Equality, ctx->op->getType() == KuraLexer::EQ ? BinaryOp::kEq : BinaryOp::kNeq);
}

auto ExprBuilder::visitLogicalAndExpr(KuraParser::LogicalAndExprContext* ctx) -> std::any {
  DEFINE_BINARY_VISIT(LogicalAnd, BinaryOp::kBinaryAnd);
}

auto ExprBuilder::visitLogicalOrExpr(KuraParser::LogicalOrExprContext* ctx) -> std::any {
  DEFINE_BINARY_VISIT(LogicalOr, BinaryOp::kBinaryOr);
}
#undef DEFINE_BINARY_VISIT

auto ExprBuilder::visitPrimaryExpr(KuraParser::PrimaryExprContext* ctx) -> std::any {
  return Build(ctx->primary());
}

auto ExprBuilder::visitParenExpr(KuraParser::ParenExprContext* ctx) -> std::any {
  return Build(ctx->expression());
}

// ╭──────────╮
// │ Literals │
// ╰──────────╯
auto ExprBuilder::visitNumberLit(KuraParser::NumberLitContext* ctx) -> std::any {
  return Literal(Number::New(std::stod(ctx->NUMBER()->getText())));
}

auto ExprBuilder::visitStringLit(KuraParser::StringLitContext* ctx) -> std::any {
  return Literal(String::New(UnescapeString(ctx->STRING()->getText())));
}

auto ExprBuilder::visitTrueLit(KuraParser::TrueLitContext* ctx) -> std::any {
  return Literal(Bool::True());
}

auto ExprBuilder::visitFalseLit(KuraParser::FalseLitContext* ctx) -> std::any {
  return Literal(Bool::False());
}

auto ExprBuilder::visitNoneLit(KuraParser::NoneLitContext* ctx) -> std::any {
  return Literal(None::Get());
}

// Px / Pct / Fr / Color are distinct types in v1 and are never implicitly
// converted to Number, so each gets its own object type.
auto ExprBuilder::BuildUnit(KuraParser::UnitLiteralContext* ctx, const bool negate) -> Object* {
  const auto sign = negate ? -1.0 : 1.0;
  const auto text = ctx->getText();
  if (ctx->PX_LIT())
    return Px::New(sign * std::stod(text.substr(0, text.size() - 2)));
  if (ctx->PCT_LIT())
    return Pct::New(sign * std::stod(text.substr(0, text.size() - 1)));
  if (ctx->FR_LIT())
    return Fr::New(sign * std::stod(text.substr(0, text.size() - 2)));
  if (negate)
    Error(ctx, "a color cannot be negated");
  return Color::New(text);
}

auto ExprBuilder::visitUnitLit(KuraParser::UnitLitContext* ctx) -> std::any {
  return Literal(BuildUnit(ctx->unitLiteral(), false));
}

auto ExprBuilder::visitVarRef(KuraParser::VarRefContext* ctx) -> std::any {
  return LoadName(ctx, ctx->IDENT()->getText());
}

auto ExprBuilder::visitVariantRef(KuraParser::VariantRefContext* ctx) -> std::any {
  return VariantExpr::New(ctx->UPPER_IDENT()->getText());
}

auto ExprBuilder::visitSeqLit(KuraParser::SeqLitContext* ctx) -> std::any {
  ExprList items{};
  for (const auto item : ctx->seqLiteral()->expression())
    items.push_back(Build(item));
  return ListExpr::New(std::move(items));
}

auto ExprBuilder::visitLambdaLit(KuraParser::LambdaLitContext* ctx) -> std::any {
  const auto lambda = ctx->lambda();
  LocalsScope lambda_scope(GetOwner());
  LambdaExpr::ParamList params{};
  for (const auto param : lambda->IDENT())
    params.push_back(CreateLocal(lambda, param->getText()));
  return LambdaExpr::New(std::move(params), BuildSeq(lambda->expression()));
}

auto ExprBuilder::visitUiLit(KuraParser::UiLitContext* ctx) -> std::any {
  UIExprBuilder builder(this);
  return builder.Build(ctx->uiExpr());
}

// ╭─────────╮
// │ Records │
// ╰─────────╯
auto ExprBuilder::visitRecordLit(KuraParser::RecordLitContext* ctx) -> std::any {
  return visitRecordLiteral(ctx->recordLiteral());
}

auto ExprBuilder::visitRecordLiteral(KuraParser::RecordLiteralContext* ctx) -> std::any {
  // Entries keep their source order: later entries override earlier ones.
  ExprList entries{};
  for (const auto entry : ctx->recordEntry()) {
    if (const auto prop = dynamic_cast<KuraParser::PropertyEntryContext*>(entry)) {
      const auto property = Property::FindOrCreate(String::New(prop->IDENT()->getText()), Type::NoneType());
      entries.push_back(StorePropertyExpr::New(property, Build(prop->expression())));
    } else if (const auto spread = dynamic_cast<KuraParser::SpreadEntryContext*>(entry)) {
      entries.push_back(SpreadExpr::New(Build(spread->expression())));
    }
  }
  return RecordExpr::NewOrdered(std::move(entries));
}

// ╭────────╮
// │ Blocks │
// ╰────────╯
auto ExprBuilder::BuildStatement(KuraParser::StatementContext* ctx) -> Expr* {
  return BuildBinding(ctx->directive(), ctx->letBinding(), ctx->redefinition());
}

auto ExprBuilder::BuildBinding(const std::vector<KuraParser::DirectiveContext*>& directives,
                               KuraParser::LetBindingContext* let, KuraParser::RedefinitionContext* redef)
    -> StoreLocalExpr* {
  StoreLocalExpr* store = nullptr;
  if (let) {
    // Evaluate the value first: `let x = x + 1` refers to the previous `x`.
    const auto value = Build(let->expression());
    store = StoreLocalExpr::New(DeclareLocal(let, let->IDENT()->getText()), value);
    store->SetAnnotation(BuildType(let->typeExpr()));
  } else {
    const auto name = redef->IDENT()->getText();
    if (!GetScope()->GetLocalRecursive(name))
      Error(redef, "cannot redefine '" + name + "': it is not defined");
    const auto value = Build(redef->expression());
    store = StoreLocalExpr::New(DeclareLocal(redef, name), value);
  }
  store->SetDirectives(BuildDirectives(directives));
  return store;
}

auto ExprBuilder::BuildBlock(antlr4::ParserRuleContext* ctx, const std::vector<KuraParser::StatementContext*>& statements,
                             KuraParser::ExpressionContext* result) -> SeqExpr* {
  LocalsScope block_scope(GetOwner());
  ExprList body{};
  for (const auto stmt : statements)
    body.push_back(BuildStatement(stmt));
  // A block with no final expression has the value None.
  body.push_back(result ? Build(result) : Literal(None::Get()));
  return SeqExpr::New(std::move(body));
}

auto ExprBuilder::visitBlockLit(KuraParser::BlockLitContext* ctx) -> std::any {
  return visitBlockExpr(ctx->blockExpr());
}

auto ExprBuilder::visitBlockExpr(KuraParser::BlockExprContext* ctx) -> std::any {
  return static_cast<Expr*>(BuildBlock(ctx, ctx->statement(), ctx->expression()));
}

auto ExprBuilder::visitBlock(KuraParser::BlockContext* ctx) -> std::any {
  return static_cast<Expr*>(BuildBlock(ctx, ctx->statement(), ctx->expression()));
}

// ╭──────────────╮
// │ Control flow │
// ╰──────────────╯
auto ExprBuilder::visitIfLit(KuraParser::IfLitContext* ctx) -> std::any {
  return visitIfExpr(ctx->ifExpr());
}

auto ExprBuilder::visitIfExpr(KuraParser::IfExprContext* ctx) -> std::any {
  const auto condition = Build(ctx->expression());
  const auto blocks = ctx->block();
  const auto then_expr = BuildSeq(blocks.at(0));
  // else-if chains arrive as a nested ifExpr; wrap it so Else is always a Seq.
  const auto else_expr = ctx->ifExpr() ? BuildSeq(ctx->ifExpr()) : BuildSeq(blocks.at(1));
  return IfExpr::New(condition, then_expr, else_expr);
}

auto ExprBuilder::visitMatchLit(KuraParser::MatchLitContext* ctx) -> std::any {
  return visitMatchExpr(ctx->matchExpr());
}

auto ExprBuilder::BuildLiteralPattern(KuraParser::LiteralPatternContext* ctx) -> PatternExpr* {
  Expr* literal = nullptr;
  if (ctx->NUMBER()) {
    const auto value = std::stod(ctx->NUMBER()->getText());
    literal = Literal(Number::New(ctx->MINUS() ? -value : value));
  } else if (ctx->STRING()) {
    literal = Literal(String::New(UnescapeString(ctx->STRING()->getText())));
  } else if (ctx->TRUE()) {
    literal = Literal(Bool::True());
  } else if (ctx->FALSE()) {
    literal = Literal(Bool::False());
  } else {
    literal = Literal(BuildUnit(ctx->unitLiteral(), ctx->MINUS() != nullptr));
  }
  return LiteralPatternExpr::New(literal->AsLiteral());
}

auto ExprBuilder::BuildPattern(KuraParser::PatternContext* ctx) -> PatternExpr* {
  if (dynamic_cast<KuraParser::WildcardPatternContext*>(ctx))
    return WildcardPatternExpr::New();
  if (dynamic_cast<KuraParser::NonePatternContext*>(ctx))
    return LiteralPatternExpr::New(Literal(None::Get())->AsLiteral());
  if (const auto lit = dynamic_cast<KuraParser::LiteralPatternAltContext*>(ctx))
    return BuildLiteralPattern(lit->literalPattern());
  if (const auto binding = dynamic_cast<KuraParser::BindingPatternContext*>(ctx))
    return BindingPatternExpr::New(CreateLocal(binding, binding->IDENT()->getText()));
  if (const auto variant = dynamic_cast<KuraParser::VariantPatternContext*>(ctx)) {
    const auto payload = variant->pattern() ? BuildPattern(variant->pattern()) : nullptr;
    return VariantPatternExpr::New(variant->UPPER_IDENT()->getText(), payload);
  }
  Error(ctx, "invalid match pattern");
}

auto ExprBuilder::visitMatchExpr(KuraParser::MatchExprContext* ctx) -> std::any {
  const auto expr = MatchExpr::New(Build(ctx->expression()))->AsMatch();
  for (const auto arm : ctx->matchArm()) {
    // Names bound by the pattern are visible in the arm body only.
    LocalsScope arm_scope(GetOwner());
    const auto pattern = BuildPattern(arm->pattern());
    const auto body = BuildSeq(arm->expression());
    if (pattern->IsWildcardPattern()) {
      if (expr->HasWildcard())
        Error(arm, "match already has a wildcard arm");
      expr->SetWildcard(body);
      continue;
    }
    expr->AppendCase(CaseExpr::New(pattern, body)->AsCase());
  }
  return static_cast<Expr*>(expr);
}

auto ExprBuilder::visitForLit(KuraParser::ForLitContext* ctx) -> std::any {
  return visitForExpr(ctx->forExpr());
}

auto ExprBuilder::visitForExpr(KuraParser::ForExprContext* ctx) -> std::any {
  // expressions: iterable, [guard], [initial], body
  const auto expressions = ctx->expression();
  size_t next = 0;
  const auto iterable = Build(expressions.at(next++));
  KuraParser::ExpressionContext* guard_ctx = ctx->IF() ? expressions.at(next++) : nullptr;
  // The initial value is evaluated outside the loop: neither the loop variable
  // nor the accumulator is visible to it.
  Expr* initial = ctx->FOLD() ? Build(expressions.at(next++)) : nullptr;
  const auto body_ctx = expressions.at(next);

  // `item` is only visible to the guard and the body.
  LocalsScope loop_scope(GetOwner());
  const auto binding = ctx->IDENT(0)->getText();
  const auto binding_local = CreateLocal(ctx, binding);
  Expr* guard = guard_ctx ? Build(guard_ctx) : nullptr;

  if (!ctx->FOLD())
    return ListComprehensionExpr::New(iterable, Build(body_ctx), binding, guard);

  const auto accumulator = ctx->IDENT(1)->getText();
  if (accumulator == binding)
    Error(ctx, "the fold accumulator and the loop variable are both named '" + binding + "'");
  const auto accumulator_local = CreateLocal(ctx, accumulator);
  return FoldExpr::New(iterable, binding_local, accumulator_local, initial, Build(body_ctx), guard);
}

// ╭────────────╮
// │ UI element │
// ╰────────────╯
static inline auto ParseNodeExprKind(const std::string& rhs) -> std::optional<NodeExpr::Kind> {
  static const std::unordered_map<std::string, NodeExpr::Kind> kKinds{
#define DEFINE_ENTRY(Name, Text) {Text, NodeExpr::Kind::k##Name},
      DEFINE_ENTRY(Document, "document") DEFINE_ENTRY(Fragment, "fragment") DEFINE_ENTRY(Block, "block")
      DEFINE_ENTRY(Button, "button") DEFINE_ENTRY(List, "list") DEFINE_ENTRY(Text, "text")
      DEFINE_ENTRY(Image, "image") DEFINE_ENTRY(Viewport, "viewport") DEFINE_ENTRY(Canvas, "canvas")
      DEFINE_ENTRY(Input, "input") DEFINE_ENTRY(Scroll, "scroll")
#undef DEFINE_ENTRY
  };
  const auto pos = kKinds.find(rhs);
  if (pos == std::end(kKinds))
    return std::nullopt;
  return {pos->second};
}

auto UIExprBuilder::Build(KuraParser::UiExprContext* ctx) -> Expr* {
  const auto tag = ctx->IDENT()->getText();
  const auto kind = ParseNodeExprKind(tag);
  if (!kind)
    ExprBuilder::Error(ctx, "unknown UI element '#" + tag + "'");

  NodeExpr::PropertyList properties{};
  if (const auto props = ctx->uiProps()) {
    if (const auto list = props->uiArgList()) {
      for (const auto arg : list->uiArg()) {
        if (const auto named = dynamic_cast<KuraParser::NamedUiArgContext*>(arg)) {
          const auto property = Property::FindOrCreate(String::New(named->IDENT()->getText()), Type::NoneType());
          properties.push_back(StorePropertyExpr::New(property, parent_->Build(named->expression())));
        } else if (const auto positional = dynamic_cast<KuraParser::PositionalUiArgContext*>(arg)) {
          // Views are not specified yet: a positional argument, as in
          // `#text(model.message)`, is stored as the `value` property.
          const auto property = Property::FindOrCreate(String::New("value"), Type::NoneType());
          properties.push_back(StorePropertyExpr::New(property, parent_->Build(positional->expression())));
        } else {
          ExprBuilder::Unsupported(arg, "spread in UI element arguments");
        }
      }
    }
  }

  const auto node = NodeExpr::New(*kind, std::move(properties))->AsNode();
  node->SetDirectives(parent_->BuildDirectives(ctx->directive()));
  if (ctx->uiChildren())
    BuildChildren(node, ctx->uiChildren());
  return static_cast<Expr*>(node);
}

auto UIExprBuilder::BuildChildren(NodeExpr* node, KuraParser::UiChildrenContext* ctx) -> void {
  ExprBuilder::LocalsScope children_scope(parent_->GetOwner());
  for (const auto child : ctx->uiChild()) {
    if (const auto element = dynamic_cast<KuraParser::ElementChildContext*>(child)) {
      node->Append(Build(element->uiExpr()));
    } else if (const auto binding = dynamic_cast<KuraParser::BindingChildContext*>(child)) {
      node->Append(parent_->BuildStatement(binding->statement()));
    }
  }
}
}  // namespace kura::expr
