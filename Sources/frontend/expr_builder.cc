#include "frontend/expr_builder.h"

#include <cctype>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "function.h"
#include "module.h"
#include "object.h"

namespace kura::expr {
using syntax::Node;

// ╭────────╮
// │ Errors │
// ╰────────╯
void ExprBuilder::Error(const syntax::Span& where, const std::string& message) const {
  const auto position = tree_.GetSource().GetPosition(where.begin);
  throw BuildError(position.line + 1, position.column, message, where);
}

void ExprBuilder::Error(const Node* where, const std::string& message) const {
  Error(where != nullptr ? where->span : syntax::Span{}, message);
}

void ExprBuilder::Unsupported(const Node* where, const std::string& what) const {
  Error(where, what + " is valid v1 syntax but the Expr IR has no node for it yet");
}

// ╭─────────╮
// │ Helpers │
// ╰─────────╯
namespace {
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

auto ExprBuilder::ToSeq(Expr* expr) -> SeqExpr* {
  if (expr->IsSeq())
    return expr->AsSeq();
  return SeqExpr::New(expr);
}

auto ExprBuilder::BuildSeq(const Node* node) -> SeqExpr* {
  return ToSeq(Build(node));
}

auto ExprBuilder::LoadName(const Node* where, const std::string& name) -> Expr* {
  const auto local = GetScope()->GetLocalRecursive(name);
  if (!local)
    Error(where, "undefined name '" + name + "'");
  return LoadLocalExpr::New(local);
}

auto ExprBuilder::CreateLocal(const syntax::Span& where, const std::string& name) -> LocalVariable* {
  const auto local = GetScope()->CreateLocal(name);
  if (!local)
    Error(where, "'" + name + "' is already defined in this scope");
  return local;
}

auto ExprBuilder::DeclareLocal(const syntax::Span& where, const std::string& name) -> LocalVariable* {
  if (GetScope() == module_scope_ && function_names_.contains(name))
    Error(where, "'" + name + "' is already defined as a function");
  const auto existing = GetScope()->GetLocal(name);
  if (existing)
    return existing;
  return CreateLocal(where, name);
}

auto ExprBuilder::BuildArgs(const std::vector<Node*>& args) -> ExprList {
  ExprList result{};
  for (const auto* arg : args)
    result.push_back(Build(arg));
  return result;
}

auto ExprBuilder::BuildCall(Expr* target, ExprList args) -> Expr* {
  return CallExpr::New(target, std::move(args));
}

// ╭──────────────╮
// │ Declarations │
// ╰──────────────╯
auto ModuleBuilder::Build(const syntax::SyntaxTree& tree) -> Module* {
  ExprBuilder builder(this, tree);
  return builder();
}

auto ExprBuilder::BuildModule() -> Module* {
  // TODO(@s0cks): get module name from the syntax tree
  const auto m = Module::New("Kura");
  module_ = m;
  module_scope_ = GetScope();
  const auto& items = tree_.GetRoot()->items;

  // Pass 0: `use` declarations. Their aliases are known before anything else
  // is built, so `alias.name` can be told apart from a property access.
  for (const auto* item : items) {
    if (const auto* use = item->As<syntax::UseDecl>())
      m->AddUse(BuildUseDecl(use));
  }

  // Pass 1: declare every function so calls may refer to functions declared
  // later in the file. (Recursion is rejected later, by the call-graph check.)
  // An `extern fn` with a body is a function that C can call.
  struct Pending {
    const Node* decl;
    syntax::Span where;
    std::string name;
    const std::vector<Node*>* directives;
    const std::vector<Node*>* params;
    const Node* result;
    const Node* body;
    bool exported;
    bool imported;
    Function* func;
  };
  std::vector<Pending> pending{};
  for (const auto* item : items) {
    Pending next{item, {}, {}, nullptr, nullptr, nullptr, nullptr, false, false, nullptr};
    if (const auto* fn = item->As<syntax::FnDecl>()) {
      next.where = fn->name.span;
      next.name = fn->name.text;
      next.directives = &fn->directives;
      next.params = &fn->params;
      next.result = fn->result;
      next.body = fn->body;
    } else if (const auto* ext = item->As<syntax::ExternDecl>()) {
      next.where = ext->c_name.span;
      next.name = ext->c_name.GetValue();
      next.directives = &ext->directives;
      next.params = &ext->params;
      next.result = ext->result;
      next.body = ext->body;
      next.exported = next.body != nullptr;
      next.imported = next.body == nullptr;
    } else {
      continue;
    }
    if (function_names_.contains(next.name))
      Error(next.where, "function '" + next.name + "' is already defined");
    next.func = Function::New(next.name);
    if (!GetScope()->CreateLocal(next.func->GetName()))
      Error(next.where, "failed to declare function '" + next.name + "'");
    function_names_.insert(next.name);
    if (next.exported) {
      next.func->SetCSignature(BuildCSignature(item->As<syntax::ExternDecl>(), next.name));
      next.func->SetCExport(true);
    }
    if (next.imported) {
      const auto signature = BuildCSignature(item->As<syntax::ExternDecl>(), next.name);
      next.func->SetCSignature(signature);
      next.func->SetDirectives(BuildDirectives(*next.directives));
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
  for (const auto* item : items) {
    if (const auto* type = item->As<syntax::TypeDecl>()) {
      m->AddType(BuildTypeDecl(type));
    } else if (const auto* binding = item->As<syntax::Binding>()) {
      m->AddBinding(BuildBinding(binding));
    } else if (item->Is<syntax::Error>()) {
      Error(item, "syntax error");  // can't happen: the tree was checked to be valid
    }
  }

  // Pass 3: function bodies.
  for (const auto& [decl, where, name, directives, params, result, body, exported, imported, func] : pending) {
    LocalsScope func_scope(GetOwner());
    Function::ParameterList parameters{};
    for (const auto* param_node : *params) {
      const auto* param = param_node->As<syntax::Param>();
      const auto local = CreateLocal(param->span, param->name.text);
      parameters.push_back({local, BuildType(param->type)});
    }
    func->SetParameters(std::move(parameters));
    if (result)
      func->SetReturnType(BuildType(result));
    func->SetDirectives(BuildDirectives(*directives));
    func->SetCExport(exported);

    LocalsScope body_scope(GetOwner());
    func->SetBody(BuildSeq(body));
    m->AddFunction(func);
  }
  return m;
}

auto ExprBuilder::BuildUseDecl(const syntax::UseDecl* ctx) -> UseDecl* {
  const syntax::Span where = ctx->has_header ? ctx->header_span : ctx->span;
  const auto& header = ctx->header;
  std::string alias{};

  if (ctx->has_alias) {
    alias = ctx->alias.text;
  } else if (const auto derived = DeriveAlias(header)) {
    alias = *derived;
  } else if (header.size() < 3 || header.substr(header.size() - 2) != ".h") {
    Error(where, "'" + header + "' is not a C header: use \"name.h\" (Kura modules are not supported yet)");
  } else {
    Error(where, "cannot derive a name from '" + header + "'; write: use \"" + header + "\" as <name>");
  }

  if (alias == "c")
    Error(where,
          "'c' is the name of the built-in C types (c.int, c.float, ...); write: use \"" + header + "\" as <name>");
  if (!use_aliases_.insert(alias).second)
    Error(where, "'" + alias + "' already names another use; write: use \"" + header + "\" as <name>");

  std::string problem{};
  auto loaded = CHeader::Load(header, &problem);
  if (!loaded)
    Error(where, "can't use \"" + header + "\": " + problem);

  headers_.emplace(alias, std::move(loaded));
  // `@pure use "m.h"` and `@pure(sqrt, sin) use "m.h"` say which of the
  // header's functions have no effects.
  std::vector<Node*> others{};
  for (auto* node : ctx->directives) {
    const auto* directive = node->As<syntax::Directive>();
    if (directive->name.text != "pure") {
      others.push_back(node);
      continue;
    }

    if (directive->args.empty()) {
      pure_headers_.insert(alias);
      continue;
    }

    for (const auto* arg_node : directive->args) {
      const auto* arg = arg_node->As<syntax::Arg>();
      if (arg->form != syntax::Arg::kPositional)
        Error(arg_node, "@pure takes the names of functions");

      const auto name = TextOf(arg->value);
      const auto function = headers_.at(alias)->FindFunction(name);
      if (!function)
        Error(arg_node, "@pure(" + name + "): '" + header + "' doesn't declare a function named '" + name + "'");

      pure_names_[alias].insert(name);
    }
  }

  return new UseDecl(header, alias, BuildDirectives(others));
}

auto ExprBuilder::ToCType(const syntax::Node* where, TypeExpr* type, const bool is_result, const std::string& what)
    -> CType {
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

auto ExprBuilder::ResolveImport(const syntax::Node* ctx, const std::string& alias, const std::string& member) -> Expr* {
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

auto ExprBuilder::BuildCSignature(const syntax::ExternDecl* ctx, const std::string& name) -> CSignature* {
  const auto signature = new CSignature();
  const auto exported = ctx->body != nullptr;
  signature->symbol = ctx->has_symbol ? ctx->symbol : exported ? CSymbolName(name) : name;
  if (ctx->variadic) {
    if (exported)
      Error(ctx->c_name.span, "extern fn " + name + ": a function C calls can't be variadic");
    signature->variadic = true;
  }
  for (const auto* param_node : ctx->params) {
    const auto* param = param_node->As<syntax::Param>();
    const auto what = name + " parameter '" + param->name.text + "'";
    signature->names.push_back(param->name.text);
    signature->params.push_back(ToCType(param, BuildType(param->type), false, what));
  }
  signature->result = ToCType(ctx, BuildType(ctx->result), true, name);
  if (exported && (signature->HasOuts() || signature->HasCallbacks()))
    Error(ctx->c_name.span, "extern fn " + name + ": a function C calls can't have c.out or callback parameters");
  return signature;
}

// ╭────────────╮
// │ Directives │
// ╰────────────╯
// Directives are hints (wiki: Language-Directives); they are recorded on the
// node they annotate and may be ignored by every later pass.
auto ExprBuilder::BuildDirectives(const std::vector<Node*>& directives) -> DirectiveList {
  DirectiveList result{};
  for (const auto* node : directives) {
    const auto* d = node->As<syntax::Directive>();
    const auto directive = new Directive();
    directive->name = d->name.text;
    for (const auto* arg_node : d->args) {
      const auto* arg = arg_node->As<syntax::Arg>();
      std::string name = "value";
      switch (arg->form) {
        case syntax::Arg::kNamed:
          name = arg->name.text;
          break;
        case syntax::Arg::kPositional:
          break;
        default:
          Error(arg_node, "spread is not allowed in directive arguments");
      }
      const auto property = Property::FindOrCreate(String::New(name), Type::NoneType());
      directive->args.push_back(StorePropertyExpr::New(property, Build(arg->value)));
    }
    result.push_back(directive);
  }
  return result;
}

// ╭───────╮
// │ Types │
// ╰───────╯
auto ExprBuilder::BuildType(const Node* node) -> TypeExpr* {
  if (!node)
    return nullptr;
  const auto build_all = [&](const std::vector<Node*>& nodes) {
    std::vector<TypeExpr*> types{};
    for (const auto* member : nodes)
      types.push_back(BuildType(member));
    return types;
  };

  if (const auto* onion = node->As<syntax::UnionType>())
    return new UnionTypeExpr(build_all(onion->members));

  if (const auto* variant = node->As<syntax::VariantType>())
    return new VariantTypeExpr(variant->name.text, BuildType(variant->payload));

  if (const auto* named = node->As<syntax::NamedType>())
    return new NamedTypeExpr(named->name.text, build_all(named->args));

  if (const auto* qualified = node->As<syntax::QualifiedType>()) {
    auto args = build_all(qualified->args);
    const auto& alias = qualified->qualifier.text;
    const auto name = qualified->member.GetValue();
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

  if (node->Is<syntax::NoneType>())
    return new NamedTypeExpr("None");

  if (const auto* record = node->As<syntax::RecordType>()) {
    std::vector<RecordTypeExpr::Field> fields{};
    for (const auto* field_node : record->fields) {
      const auto* field = field_node->As<syntax::Field>();
      fields.push_back({field->name.text, BuildType(field->type), field->optional});
    }
    return new RecordTypeExpr(std::move(fields));
  }

  if (const auto* function = node->As<syntax::FunctionType>())
    return new FunctionTypeExpr(build_all(function->params), BuildType(function->result));

  Error(node, "invalid type");
}

auto ExprBuilder::BuildTypeDecl(const syntax::TypeDecl* ctx) -> TypeDecl* {
  std::vector<std::string> params{};
  for (const auto& param : ctx->type_params)
    params.push_back(param.text);
  return new TypeDecl(ctx->name.text, std::move(params), BuildType(ctx->type), BuildDirectives(ctx->directives));
}

// ╭─────────────╮
// │ Expressions │
// ╰─────────────╯
auto ExprBuilder::Build(const Node* node) -> Expr* {
  switch (node->GetKind()) {
    case Node::kNumberLit:
      return Literal(NumberLiteral(node->span, node->As<syntax::NumberLit>()->text, false));
    case Node::kStringLit:
      return Literal(String::New(node->As<syntax::StringLit>()->value));
    case Node::kBoolLit:
      return Literal(node->As<syntax::BoolLit>()->value ? Bool::True() : Bool::False());
    case Node::kNoneLit:
      return Literal(None::Get());
    case Node::kUnitLit:
      return Literal(BuildUnit(node->As<syntax::UnitLit>(), false));
    case Node::kVarRef:
      return LoadName(node, node->As<syntax::VarRef>()->name.text);
    case Node::kVariantRef:
      return VariantExpr::New(node->As<syntax::VariantRef>()->name.text);
    case Node::kSeqLit: {
      ExprList items{};
      for (const auto* item : node->As<syntax::SeqLit>()->elements)
        items.push_back(Build(item));
      return ListExpr::New(std::move(items));
    }
    case Node::kRecordLit:
      return BuildRecord(node->As<syntax::RecordLit>());
    case Node::kBlock:
      return BuildBlock(node->As<syntax::Block>());
    case Node::kIfExpr:
      return BuildIf(node->As<syntax::IfExpr>());
    case Node::kMatchExpr:
      return BuildMatch(node->As<syntax::MatchExpr>());
    case Node::kForExpr:
      return BuildFor(node->As<syntax::ForExpr>());
    case Node::kLambda:
      return BuildLambda(node->As<syntax::Lambda>());
    case Node::kUiElement:
      return BuildUi(node->As<syntax::UiElement>());
    case Node::kParen:
      return Build(node->As<syntax::Paren>()->inner);
    case Node::kUnary: {
      const auto* unary = node->As<syntax::Unary>();
      const auto op = unary->op == syntax::Unary::kNeg ? UnaryOp::kMinus : UnaryOp::kBang;
      return UnaryExpr::New(op, Build(unary->operand));
    }
    case Node::kBinary:
      return BuildBinary(node->As<syntax::Binary>());
    case Node::kMember:
      return BuildMember(node->As<syntax::Member>());
    case Node::kCall:
      return BuildCallExpr(node->As<syntax::Call>());
    case Node::kPipe:
      return BuildPipe(node->As<syntax::Pipe>());
    default:
      Error(node, "failed to build expression");
  }
}

auto ExprBuilder::BuildBinary(const syntax::Binary* binary) -> Expr* {
  BinaryOp op;
  switch (binary->op) {
    case syntax::Binary::kAdd:
      op = BinaryOp::kAdd;
      break;
    case syntax::Binary::kSub:
      op = BinaryOp::kSubtract;
      break;
    case syntax::Binary::kMul:
      op = BinaryOp::kMultiply;
      break;
    case syntax::Binary::kDiv:
      op = BinaryOp::kDivide;
      break;
    case syntax::Binary::kLt:
      op = BinaryOp::kLessThan;
      break;
    case syntax::Binary::kLe:
      op = BinaryOp::kLessThanEqual;
      break;
    case syntax::Binary::kGt:
      op = BinaryOp::kGreaterThan;
      break;
    case syntax::Binary::kGe:
      op = BinaryOp::kGreaterThanEqual;
      break;
    case syntax::Binary::kEq:
      op = BinaryOp::kEq;
      break;
    case syntax::Binary::kNeq:
      op = BinaryOp::kNeq;
      break;
    case syntax::Binary::kAnd:
      op = BinaryOp::kBinaryAnd;
      break;
    default:
      op = BinaryOp::kBinaryOr;
      break;
  }
  return BinaryExpr::New(op, Build(binary->lhs), Build(binary->rhs));
}

auto ExprBuilder::BuildPipe(const syntax::Pipe* pipe) -> Expr* {
  // `x |> f(y)` is `f(x, y)`; the parser guarantees the right side is a call.
  const auto lhs = Build(pipe->lhs);
  const Node* callee = pipe->callee;

  Expr* target = nullptr;
  if (const auto* paren = callee->As<syntax::Paren>()) {
    target = Build(paren->inner);
  } else {
    // unwind `head.a.b` into the head and its members
    std::vector<const syntax::Member*> members{};
    const Node* head = callee;
    while (const auto* member = head->As<syntax::Member>()) {
      members.push_back(member);
      head = member->object;
    }
    std::reverse(members.begin(), members.end());
    if (head->Is<syntax::VariantRef>() && members.empty()) {
      // `payload |> Click()` is `Click(payload)`.
      if (!pipe->args.empty())
        Error(callee, "a piped variant takes no further arguments");
      return VariantExpr::New(head->As<syntax::VariantRef>()->name.text, lhs);
    }
    const auto head_name =
        head->Is<syntax::VarRef>() ? head->As<syntax::VarRef>()->name.text : head->As<syntax::VariantRef>()->name.text;
    size_t first = 0;
    if (use_aliases_.contains(head_name) && !members.empty()) {
      target = ResolveImport(callee, head_name, members.front()->member.GetValue());
      first = 1;
    } else {
      target = LoadName(callee, head_name);
    }
    for (size_t idx = first; idx < members.size(); idx++) {
      const auto property = Property::FindOrCreate(String::New(members[idx]->member.GetValue()), Type::NoneType());
      target = GetPropertyExpr::New(target, property);
    }
  }

  ExprList args{lhs};
  for (const auto arg : BuildArgs(pipe->args))
    args.push_back(arg);
  return BuildCall(target, std::move(args));
}

auto ExprBuilder::BuildMember(const syntax::Member* ctx) -> Expr* {
  const auto member = ctx->member.GetValue();
  // `alias.name` names something from a header, not a property
  if (ctx->object->Is<syntax::VarRef>() || ctx->object->Is<syntax::VariantRef>()) {
    const auto base = TextOf(ctx->object);
    if (use_aliases_.contains(base))
      return ResolveImport(ctx, base, member);
  }
  const auto target = Build(ctx->object);
  const auto property = Property::FindOrCreate(String::New(member), Type::NoneType());
  return GetPropertyExpr::New(target, property);
}

auto ExprBuilder::BuildCallExpr(const syntax::Call* ctx) -> Expr* {
  // `Click(payload)` constructs a variant rather than calling a function.
  if (const auto* variant = ctx->callee->As<syntax::VariantRef>()) {
    const auto& name = variant->name.text;
    const auto args = BuildArgs(ctx->args);
    if (args.size() != 1)
      Error(ctx, "variant '" + name + "' takes exactly one payload");
    return VariantExpr::New(name, args.front());
  }
  const auto target = Build(ctx->callee);
  return BuildCall(target, BuildArgs(ctx->args));
}

auto ExprBuilder::NumberLiteral(const syntax::Span& where, const std::string& text, const bool negate) const
    -> Object* {
  if (text.find('.') != std::string::npos) {
    const auto value = std::stod(text);
    return Float::New(negate ? -value : value);
  }
  try {
    const auto value = std::stoll(negate ? "-" + text : text);
    return Int::New(value);
  } catch (const std::out_of_range&) {
    Error(where, "integer literal `" + text + "` does not fit in a 64-bit Int");
  }
}

auto ExprBuilder::BuildUnit(const syntax::UnitLit* ctx, const bool negate) const -> Object* {
  const auto sign = negate ? -1.0 : 1.0;
  const auto& text = ctx->text;
  switch (ctx->unit) {
    case syntax::UnitLit::kPx:
      return Px::New(sign * std::stod(text.substr(0, text.size() - 2)));
    case syntax::UnitLit::kPct:
      return Pct::New(sign * std::stod(text.substr(0, text.size() - 1)));
    case syntax::UnitLit::kFr:
      return Fr::New(sign * std::stod(text.substr(0, text.size() - 2)));
    default:
      break;
  }

  return Color::Parse(text);
}

auto ExprBuilder::BuildLambda(const syntax::Lambda* lambda) -> Expr* {
  LocalsScope lambda_scope(GetOwner());
  LambdaExpr::ParamList params{};
  for (const auto& param : lambda->params)
    params.push_back(CreateLocal(lambda->span, param.text));
  return LambdaExpr::New(std::move(params), BuildSeq(lambda->body));
}

auto ExprBuilder::BuildRecord(const syntax::RecordLit* ctx) -> Expr* {
  ExprList entries{};
  for (const auto* entry_node : ctx->entries) {
    const auto* entry = entry_node->As<syntax::Arg>();
    if (entry->form == syntax::Arg::kNamed) {
      const auto property = Property::FindOrCreate(String::New(entry->name.text), Type::NoneType());
      entries.push_back(StorePropertyExpr::New(property, Build(entry->value)));
    } else if (entry->form == syntax::Arg::kSpread) {
      entries.push_back(SpreadExpr::New(Build(entry->value)));
    }
  }

  return RecordExpr::NewOrdered(std::move(entries));
}

// ╭────────╮
// │ Blocks │
// ╰────────╯
auto ExprBuilder::BuildBinding(const syntax::Binding* binding) -> StoreLocalExpr* {
  StoreLocalExpr* store = nullptr;
  if (binding->is_let) {
    // Evaluate the value first: `let x = x + 1` refers to the previous `x`.
    const auto value = Build(binding->value);
    store = StoreLocalExpr::New(DeclareLocal(binding->name.span, binding->name.text), value);
    store->SetAnnotation(BuildType(binding->annotation));
  } else {
    const auto& name = binding->name.text;
    if (!GetScope()->GetLocalRecursive(name))
      Error(binding->name.span, "cannot redefine '" + name + "': it is not defined");
    const auto value = Build(binding->value);
    store = StoreLocalExpr::New(DeclareLocal(binding->name.span, name), value);
  }
  store->SetDirectives(BuildDirectives(binding->directives));
  return store;
}

auto ExprBuilder::BuildBlock(const syntax::Block* block) -> SeqExpr* {
  LocalsScope block_scope(GetOwner());
  ExprList body{};
  for (const auto* stmt : block->statements)
    body.push_back(BuildBinding(stmt->As<syntax::Binding>()));
  // A block with no final expression has the value None.
  body.push_back(block->result ? Build(block->result) : Literal(None::Get()));
  return SeqExpr::New(std::move(body));
}

// ╭──────────────╮
// │ Control flow │
// ╰──────────────╯
auto ExprBuilder::BuildIf(const syntax::IfExpr* ctx) -> Expr* {
  const auto condition = Build(ctx->condition);
  const auto then_expr = BuildSeq(ctx->then_branch);
  // else-if chains arrive as a nested IfExpr; wrap it so Else is always a Seq.
  const auto else_expr = BuildSeq(ctx->else_branch);
  return IfExpr::New(condition, then_expr, else_expr);
}

auto ExprBuilder::BuildLiteralPattern(const syntax::LiteralPattern* ctx) -> PatternExpr* {
  Expr* literal = nullptr;
  const auto* value = ctx->literal;
  if (const auto* number = value->As<syntax::NumberLit>()) {
    literal = Literal(NumberLiteral(number->span, number->text, ctx->negated));
  } else if (const auto* string = value->As<syntax::StringLit>()) {
    literal = Literal(String::New(string->value));
  } else if (const auto* boolean = value->As<syntax::BoolLit>()) {
    literal = Literal(boolean->value ? Bool::True() : Bool::False());
  } else if (const auto* unit = value->As<syntax::UnitLit>()) {
    literal = Literal(BuildUnit(unit, ctx->negated));
  } else {
    Error(ctx, "invalid match pattern");
  }
  return LiteralPatternExpr::New(literal->AsLiteral());
}

auto ExprBuilder::BuildPattern(const Node* node) -> PatternExpr* {
  if (node->Is<syntax::WildcardPattern>())
    return WildcardPatternExpr::New();
  if (node->Is<syntax::NonePattern>())
    return LiteralPatternExpr::New(Literal(None::Get())->AsLiteral());
  if (const auto* lit = node->As<syntax::LiteralPattern>())
    return BuildLiteralPattern(lit);
  if (const auto* binding = node->As<syntax::BindingPattern>())
    return BindingPatternExpr::New(CreateLocal(binding->span, binding->name.text));
  if (const auto* variant = node->As<syntax::VariantPattern>()) {
    const auto payload = variant->payload ? BuildPattern(variant->payload) : nullptr;
    return VariantPatternExpr::New(variant->name.text, payload);
  }
  Error(node, "invalid match pattern");
}

auto ExprBuilder::BuildMatch(const syntax::MatchExpr* ctx) -> Expr* {
  const auto expr = MatchExpr::New(Build(ctx->scrutinee))->AsMatch();
  for (const auto* arm_node : ctx->arms) {
    const auto* arm = arm_node->As<syntax::MatchArm>();
    // Names bound by the pattern are visible in the arm body only.
    LocalsScope arm_scope(GetOwner());
    const auto pattern = BuildPattern(arm->pattern);
    const auto body = BuildSeq(arm->body);
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

auto ExprBuilder::BuildFor(const syntax::ForExpr* ctx) -> Expr* {
  const auto iterable = Build(ctx->iterable);
  // The initial value is evaluated outside the loop: neither the loop variable
  // nor the accumulator is visible to it.
  Expr* initial = ctx->has_fold ? Build(ctx->initial) : nullptr;

  // `item` is only visible to the guard and the body.
  LocalsScope loop_scope(GetOwner());
  const auto& binding = ctx->binding.text;
  const auto binding_local = CreateLocal(ctx->span, binding);
  Expr* guard = ctx->guard ? Build(ctx->guard) : nullptr;

  if (!ctx->has_fold)
    return ListComprehensionExpr::New(iterable, Build(ctx->body), binding_local, binding, guard);

  const auto& accumulator = ctx->accumulator.text;
  if (accumulator == binding)
    Error(ctx, "the fold accumulator and the loop variable are both named '" + binding + "'");
  const auto accumulator_local = CreateLocal(ctx->span, accumulator);
  return FoldExpr::New(iterable, binding_local, accumulator_local, initial, Build(ctx->body), guard);
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

auto ExprBuilder::BuildUi(const syntax::UiElement* ctx) -> Expr* {
  const auto& tag = ctx->tag.text;
  const auto kind = ParseNodeExprKind(tag);
  if (!kind)
    Error(ctx, "unknown UI element '#" + tag + "'");

  NodeExpr::PropertyList properties{};
  for (const auto* arg_node : ctx->props) {
    const auto* arg = arg_node->As<syntax::Arg>();
    switch (arg->form) {
      case syntax::Arg::kNamed: {
        const auto property = Property::FindOrCreate(String::New(arg->name.text), Type::NoneType());
        properties.push_back(StorePropertyExpr::New(property, Build(arg->value)));
        break;
      }
      case syntax::Arg::kPositional: {
        // Views are not specified yet: a positional argument, as in
        // `#text(model.message)`, is stored as the `value` property.
        const auto property = Property::FindOrCreate(String::New("value"), Type::NoneType());
        properties.push_back(StorePropertyExpr::New(property, Build(arg->value)));
        break;
      }
      default:
        Unsupported(arg_node, "spread in UI element arguments");
    }
  }

  const auto node = NodeExpr::New(*kind, std::move(properties))->AsNode();
  node->SetDirectives(BuildDirectives(ctx->directives));
  if (ctx->has_children)
    BuildUiChildren(node, ctx);
  return static_cast<Expr*>(node);
}

auto ExprBuilder::BuildUiChildren(NodeExpr* node, const syntax::UiElement* ctx) -> void {
  LocalsScope children_scope(GetOwner());
  for (const auto* child : ctx->children) {
    if (const auto* element = child->As<syntax::UiElement>()) {
      node->Append(BuildUi(element));
    } else if (const auto* binding = child->As<syntax::Binding>()) {
      node->Append(BuildBinding(binding));
    }
  }
}
}  // namespace kura::expr
