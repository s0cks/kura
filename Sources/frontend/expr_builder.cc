#include "frontend/expr_builder.h"

#include <cctype>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

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
namespace {
auto Unquote(const std::string& text) -> std::string {
  if (text.size() >= 2 && (text.front() == '`' || text.front() == '"') && text.back() == text.front())
    return text.substr(1, text.size() - 2);
  return text;
}

// A type from a C header, written as a Kura type expression: a struct is the
// record with the same fields.
auto TypeExprOf(const CType& c) -> TypeExpr* {
  if (c.kind == CType::kRecord) {
    std::vector<RecordTypeExpr::Field> fields{};
    for (const auto& field : c.record->fields)
      fields.push_back({field.name, TypeExprOf(field.type), false});
    return new RecordTypeExpr(std::move(fields));
  }
  TypeExpr* named = new NamedTypeExpr(c.ToName());
  if (c.nullable)
    return new UnionTypeExpr({named, new NamedTypeExpr("None")});
  return named;
}

auto IsKeyword(const std::string& name) -> bool {
  static const std::unordered_set<std::string> kKeywords{
      "let",   "fn",   "type",   "if",   "else",    "match", "for",    "in",     "fold",
      "use",   "as",   "extern", "true", "false",   "None",  "black",  "white",  "red",
      "green", "blue", "yellow", "cyan", "magenta", "gray",  "orange", "purple", "transparent"};
  return kKeywords.contains(name);
}

// Whether `name` lexes as a single IDENT or UPPER_IDENT.
auto IsPlainName(const std::string& name) -> bool {
  if (name.empty() || IsKeyword(name))
    return false;
  const auto head = static_cast<unsigned char>(name.front());
  const auto upper = std::isupper(head) != 0;
  if (!upper && !std::islower(head) && head != '_')
    return false;
  for (size_t i = 1; i < name.size(); i++) {
    const auto c = static_cast<unsigned char>(name[i]);
    if (std::isalnum(c) || c == '_')
      continue;
    const auto kebab = c == '-' && !upper && i + 1 < name.size() && name[i + 1] != '-';
    if (!kebab)
      return false;
  }
  return true;
}

// `use "math.h"` is named `math`; `use "SDL2/SDL.h"` is named `SDL`.
auto DeriveAlias(const std::string& header) -> std::optional<std::string> {
  constexpr auto kSuffix = std::string_view(".h");
  if (header.size() <= kSuffix.size() || header.substr(header.size() - kSuffix.size()) != kSuffix)
    return std::nullopt;
  const auto slash = header.find_last_of('/');
  const auto begin = slash == std::string::npos ? 0 : slash + 1;
  const auto stem = header.substr(begin, header.size() - kSuffix.size() - begin);
  if (!IsPlainName(stem))
    return std::nullopt;
  return stem;
}
}  // namespace

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
  module_ = m;
  module_scope_ = GetScope();

  // Pass 0: `use` declarations. Their aliases are known before anything else
  // is built, so `alias.name` can be told apart from a property access.
  for (const auto item : ctx->item()) {
    if (const auto use = item->useDecl())
      m->AddUse(BuildUseDecl(use, item->directive()));
  }

  // Pass 1: declare every function so calls may refer to functions declared
  // later in the file. (Recursion is rejected later, by the call-graph check.)
  // An `extern fn` with a body is a function that C can call.
  struct Pending {
    KuraParser::ItemContext* item;
    antlr4::ParserRuleContext* decl;
    std::string name;
    KuraParser::ParamListContext* params;
    KuraParser::TypeExprContext* result;
    KuraParser::ExpressionContext* body;
    bool exported;
    bool imported;
    Function* func;
  };
  std::vector<Pending> pending{};
  for (const auto item : ctx->item()) {
    Pending next{item, nullptr, {}, nullptr, nullptr, nullptr, false, false, nullptr};
    if (const auto fn = item->fnDecl()) {
      next.decl = fn;
      next.name = fn->IDENT()->getText();
      next.params = fn->paramList();
      next.result = fn->typeExpr();
      next.body = fn->expression();
    } else if (const auto ext = item->externDecl()) {
      next.decl = ext;
      next.name = Unquote(ext->cName()->getText());
      next.params = ext->externParams() ? ext->externParams()->paramList() : nullptr;
      next.result = ext->typeExpr();
      next.body = ext->expression();
      next.exported = next.body != nullptr;
      next.imported = next.body == nullptr;
    } else {
      continue;
    }
    if (function_names_.contains(next.name))
      Error(next.decl, "function '" + next.name + "' is already defined");
    next.func = Function::New(next.name);
    if (!GetScope()->CreateLocal(next.func->GetName()))
      Error(next.decl, "failed to declare function '" + next.name + "'");
    function_names_.insert(next.name);
    if (next.exported) {
      next.func->SetCSignature(BuildCSignature(item->externDecl(), next.name));
      next.func->SetCExport(true);
    }
    if (next.imported) {
      const auto signature = BuildCSignature(item->externDecl(), next.name);
      next.func->SetCSignature(signature);
      next.func->SetDirectives(BuildDirectives(item->directive()));
      for (const auto directive : next.func->GetDirectives())
        signature->pure = signature->pure || directive->name == "pure";
      m->AddFunction(next.func);
      continue;
    }
    pending.push_back(std::move(next));
  }

  // Pass 2: type declarations, extern imports and top-level bindings, in source
  // order. They are built before any function body so bodies can use every
  // top-level name.
  for (const auto item : ctx->item()) {
    if (item->typeDecl()) {
      m->AddType(BuildTypeDecl(item->typeDecl(), item->directive()));
    } else if (item->letBinding() || item->redefinition()) {
      m->AddBinding(BuildBinding(item->directive(), item->letBinding(), item->redefinition()));
    }
  }

  // Pass 3: function bodies.
  for (const auto& [item, decl, name, params, result, body, exported, imported, func] : pending) {
    LocalsScope func_scope(GetOwner());
    Function::ParameterList parameters{};
    if (params) {
      for (const auto param : params->param()) {
        const auto local = CreateLocal(param, param->IDENT()->getText());
        parameters.push_back({local, BuildType(param->typeExpr())});
      }
    }
    func->SetParameters(std::move(parameters));
    if (result)
      func->SetReturnType(BuildType(result));
    func->SetDirectives(BuildDirectives(item->directive()));
    func->SetCExport(exported);

    LocalsScope body_scope(GetOwner());
    func->SetBody(BuildSeq(body));
    m->AddFunction(func);
  }
  return m;
}

auto ExprBuilder::BuildUseDecl(KuraParser::UseDeclContext* ctx,
                               const std::vector<KuraParser::DirectiveContext*>& directives) -> UseDecl* {
  const auto header = Unquote(ctx->STRING()->getText());
  std::string alias{};
  if (const auto as = ctx->qualifier()) {
    alias = as->getText();
  } else if (const auto derived = DeriveAlias(header)) {
    alias = *derived;
  } else if (header.size() < 3 || header.substr(header.size() - 2) != ".h") {
    Error(ctx, "'" + header + "' is not a C header: use \"name.h\" (Kura modules are not supported yet)");
  } else {
    Error(ctx, "cannot derive a name from '" + header + "'; write: use \"" + header + "\" as <name>");
  }
  if (alias == "c")
    Error(ctx,
          "'c' is the name of the built-in C types (c.int, c.float, ...); write: use \"" + header + "\" as <name>");
  if (!use_aliases_.insert(alias).second)
    Error(ctx, "'" + alias + "' already names another use; write: use \"" + header + "\" as <name>");
  std::string problem{};
  auto loaded = CHeader::Load(header, &problem);
  if (!loaded)
    Error(ctx, "can't use \"" + header + "\": " + problem);
  headers_.emplace(alias, std::move(loaded));
  // `@pure use "m.h"` and `@pure(sqrt, sin) use "m.h"` say which of the
  // header's functions have no effects.
  std::vector<KuraParser::DirectiveContext*> others{};
  for (const auto directive : directives) {
    if (directive->DIRECTIVE()->getText() != "@pure") {
      others.push_back(directive);
      continue;
    }
    const auto list = directive->uiArgList();
    if (!list) {
      pure_headers_.insert(alias);
      continue;
    }
    for (const auto arg : list->uiArg()) {
      const auto positional = dynamic_cast<KuraParser::PositionalUiArgContext*>(arg);
      if (!positional)
        Error(arg, "@pure takes the names of functions");
      const auto name = positional->expression()->getText();
      const auto function = headers_.at(alias)->FindFunction(name);
      if (!function)
        Error(arg, "@pure(" + name + "): '" + header + "' doesn't declare a function named '" + name + "'");
      pure_names_[alias].insert(name);
    }
  }
  return new UseDecl(header, alias, BuildDirectives(others));
}

auto ExprBuilder::ToCType(antlr4::ParserRuleContext* where, TypeExpr* type, const bool is_result,
                          const std::string& what) -> CType {
  const auto fail = [&](const std::string& why) {
    Error(where, "extern fn " + what + ": " + why);
  };
  std::vector<TypeExpr*> alternatives{};
  if (const auto onion = type->AsUnion()) {
    alternatives = onion->GetMembers();
  } else {
    alternatives.push_back(type);
  }
  // `c.out<c.int>`: a pointer C writes through. `c.callback<(c.int) => c.int>`:
  // a function pointer, which a program fills with the name of a function.
  if (alternatives.size() == 1) {
    if (const auto named = alternatives[0]->AsNamed(); named && named->GetArgs().size() == 1) {
      if (named->GetTypeName() == "c.out") {
        if (is_result)
          fail("c.out is only for parameters");
        auto pointee = ToCType(where, named->GetArgs()[0], true, what);
        if (!pointee.IsInteger() && !pointee.IsFloat() && pointee.kind != CType::kBool)
          fail("c.out takes a number or a Bool, not " + pointee.ToString());
        auto out = CType::Make(CType::kOut);
        out.pointee = std::make_shared<CType>(pointee);
        return out;
      }
      if (named->GetTypeName() == "c.callback") {
        const auto function = named->GetArgs()[0]->AsFunction();
        if (is_result || !function)
          fail("c.callback<(...) => T> is a parameter type");
        auto callback = std::make_shared<CSignature>();
        for (const auto param : function->GetParams())
          callback->params.push_back(ToCType(where, param, false, what + " callback"));
        callback->result = ToCType(where, function->GetResult(), true, what + " callback");
        if (callback->HasOuts() || callback->HasCallbacks())
          fail("a callback can't take c.out or callback parameters");
        auto type = CType::Make(CType::kCallback);
        type.callback = callback;
        return type;
      }
    }
  }
  bool nullable = false;
  std::optional<CType> found{};
  for (const auto alternative : alternatives) {
    const auto named = alternative->AsNamed();
    if (!named || !named->GetArgs().empty())
      fail("'" + alternative->ToString() + "' is not a type C understands");
    const auto& name = named->GetTypeName();
    if (name == "None" && alternatives.size() > 1) {
      nullable = true;
      continue;
    }
    const auto c = CType::FromName(name);
    if (!c)
      fail("'" + name + "' is not a C type; use Int, Float, Bool, String, or c.int, c.float, c.ptr and the like");
    if (found)
      fail("a union of C types isn't supported");
    found = c;
  }
  if (!found)
    fail("'" + type->ToString() + "' is not a type C understands");
  if (nullable) {
    if (!found->IsPointer())
      fail("only String and c.ptr can be `| None`");
    found->nullable = true;
  }
  if (found->kind == CType::kVoid && !is_result)
    fail("None is only a result type");
  return *found;
}

auto ExprBuilder::ResolveImport(antlr4::ParserRuleContext* ctx, const std::string& alias, const std::string& member)
    -> Expr* {
  const auto& header = headers_.at(alias);
  const auto name = alias + "." + member;
  if (const auto fn = header->FindFunction(member)) {
    if (!fn->problem.empty())
      Error(ctx, name + " can't be called from Kura: " + fn->problem);
    if (imported_.insert(name).second) {
      const auto function = Function::New(name);
      const auto signature = new CSignature(fn->signature);
      signature->pure = signature->pure || pure_headers_.contains(alias) || pure_names_[alias].contains(member);
      function->SetCSignature(signature);
      if (!module_scope_->CreateLocal(function->GetName()))
        Error(ctx, "failed to declare " + name);
      module_->AddFunction(function);
    }
    return LoadName(ctx, name);
  }
  if (const auto constant = header->FindConstant(member)) {
    switch (constant->kind) {
      case CHeader::Constant::kInt:
        return Literal(Int::New(constant->integer));
      case CHeader::Constant::kFloat:
        return Literal(Float::New(constant->real));
      case CHeader::Constant::kString:
        return Literal(String::New(constant->text));
    }
  }
  Error(ctx, "'" + header->GetPath() + "' doesn't declare '" + member + "'");
}

auto ExprBuilder::BuildCSignature(KuraParser::ExternDeclContext* ctx, const std::string& name) -> CSignature* {
  const auto signature = new CSignature();
  const auto exported = ctx->expression() != nullptr;
  signature->symbol = ctx->STRING() ? Unquote(ctx->STRING()->getText()) : exported ? CSymbolName(name) : name;
  if (ctx->externParams() && ctx->externParams()->SPREAD()) {
    if (exported)
      Error(ctx, "extern fn " + name + ": a function C calls can't be variadic");
    signature->variadic = true;
  }
  const auto list = ctx->externParams() ? ctx->externParams()->paramList() : nullptr;
  if (list) {
    for (const auto param : list->param()) {
      const auto what = name + " parameter '" + param->IDENT()->getText() + "'";
      signature->names.push_back(param->IDENT()->getText());
      signature->params.push_back(ToCType(param, BuildType(param->typeExpr()), false, what));
    }
  }
  signature->result = ToCType(ctx, BuildType(ctx->typeExpr()), true, name);
  if (exported && (signature->HasOuts() || signature->HasCallbacks()))
    Error(ctx, "extern fn " + name + ": a function C calls can't have c.out or callback parameters");
  return signature;
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

  if (const auto qualified = dynamic_cast<KuraParser::QualifiedTypeContext*>(ctx)) {
    std::vector<TypeExpr*> args{};
    if (const auto type_args = qualified->typeArgs()) {
      for (const auto arg : type_args->typeExpr())
        args.push_back(BuildType(arg));
    }
    const auto alias = qualified->qualifier()->getText();
    const auto name = Unquote(qualified->cName()->getText());
    if (alias != "c" && !use_aliases_.contains(alias))
      Error(qualified, "'" + alias + "' is not a use");
    if (alias != "c") {
      const auto type = headers_.at(alias)->FindType(name);
      if (!type)
        Error(qualified, "'" + headers_.at(alias)->GetPath() + "' doesn't declare a type named '" + name + "'");
      if (!type->problem.empty())
        Error(qualified, alias + "." + name + " can't be used: " + type->problem);
      return TypeExprOf(type->type);
    }
    return new NamedTypeExpr(alias + "." + name, std::move(args));
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
  if (callee->UPPER_IDENT() && callee->memberName().empty()) {
    // `payload |> Click()` is `Click(payload)`.
    if (ctx->argumentList() && !ctx->argumentList()->expression().empty())
      Error(ctx, "a piped variant takes no further arguments");
    return VariantExpr::New(callee->UPPER_IDENT()->getText(), lhs);
  } else if (callee->expression()) {
    target = Build(callee->expression());
  } else {
    const auto head = callee->IDENT() ? callee->IDENT() : callee->UPPER_IDENT();
    const auto members = callee->memberName();
    size_t first = 0;
    if (use_aliases_.contains(head->getText()) && !members.empty()) {
      target = ResolveImport(callee, head->getText(), Unquote(members.front()->getText()));
      first = 1;
    } else {
      target = LoadName(callee, head->getText());
    }
    for (size_t idx = first; idx < members.size(); idx++) {
      const auto member = members[idx];
      const auto property = Property::FindOrCreate(String::New(Unquote(member->getText())), Type::NoneType());
      target = GetPropertyExpr::New(target, property);
    }
  }

  ExprList args{lhs};
  for (const auto arg : BuildArgs(ctx->argumentList()))
    args.push_back(arg);
  return BuildCall(ctx, target, std::move(args));
}

auto ExprBuilder::visitPropertyAccess(KuraParser::PropertyAccessContext* ctx) -> std::any {
  const auto member = Unquote(ctx->memberName()->getText());
  if (const auto primary = dynamic_cast<KuraParser::PrimaryExprContext*>(ctx->operation())) {
    const auto base = primary->primary()->getText();
    if (use_aliases_.contains(base))
      return ResolveImport(ctx, base, member);
  }
  const auto target = Build(ctx->operation());
  const auto property = Property::FindOrCreate(String::New(member), Type::NoneType());
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

#define DEFINE_BINARY_VISIT(Ctx, Op) return BinaryExpr::New((Op), Build(ctx->operation(0)), Build(ctx->operation(1)))

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
// `10` is an Int and `10.0` a Float. The lexer only allows digits with an
// optional fractional part, so a `.` is what tells them apart.
auto ExprBuilder::NumberLiteral(antlr4::ParserRuleContext* ctx, const std::string& text, const bool negate) -> Object* {
  if (text.find('.') != std::string::npos) {
    const auto value = std::stod(text);
    return Float::New(negate ? -value : value);
  }
  try {
    const auto value = std::stoll(negate ? "-" + text : text);
    return Int::New(value);
  } catch (const std::out_of_range&) {
    Error(ctx, "integer literal `" + text + "` does not fit in a 64-bit Int");
  }
}

auto ExprBuilder::visitNumberLit(KuraParser::NumberLitContext* ctx) -> std::any {
  return Literal(NumberLiteral(ctx, ctx->NUMBER()->getText(), false));
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

  return Color::Parse(text);
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

auto ExprBuilder::BuildBlock(antlr4::ParserRuleContext* ctx,
                             const std::vector<KuraParser::StatementContext*>& statements,
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
    literal = Literal(NumberLiteral(ctx, ctx->NUMBER()->getText(), ctx->MINUS() != nullptr));
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
    return ListComprehensionExpr::New(iterable, Build(body_ctx), binding_local, binding, guard);

  const auto accumulator = ctx->IDENT(1)->getText();
  if (accumulator == binding)
    Error(ctx, "the fold accumulator and the loop variable are both named '" + binding + "'");
  const auto accumulator_local = CreateLocal(ctx, accumulator);
  return FoldExpr::New(iterable, binding_local, accumulator_local, initial, Build(body_ctx), guard);
}

// ╭────────────╮
// │ UI element │
// ╰────────────╯
// The tags come from the same list as the kinds, so an element can't be added
// to one and forgotten in the other.
static inline auto ParseNodeExprKind(const std::string& rhs) -> std::optional<NodeExpr::Kind> {
  const auto kind = dom::FindKind(rhs);
  if (!kind || !dom::IsElement(*kind))
    return std::nullopt;

  switch (*kind) {
#define DEFINE_CASE(Name, Tag) \
  case dom::Kind::k##Name:     \
    return NodeExpr::Kind::k##Name;
    FOR_EACH_ELEMENT_NODE(DEFINE_CASE)
#undef DEFINE_CASE
    default:
      return std::nullopt;
  }
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
