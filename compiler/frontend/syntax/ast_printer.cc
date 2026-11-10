#include "compiler/frontend/syntax/ast_printer.h"

#include <ostream>
#include <sstream>

namespace kura::syntax {
namespace {
auto Quote(const std::string& text) -> std::string {
  std::string out = "\"";
  for (const auto c : text) {
    switch (c) {
      case '\n':
        out += "\\n";
        break;
      case '\t':
        out += "\\t";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      default:
        out.push_back(c);
    }
  }
  return out + "\"";
}

auto OpName(const Binary::Op op) -> const char* {
  switch (op) {
    case Binary::kAdd:
      return "+";
    case Binary::kSub:
      return "-";
    case Binary::kMul:
      return "*";
    case Binary::kDiv:
      return "/";
    case Binary::kLt:
      return "<";
    case Binary::kLe:
      return "<=";
    case Binary::kGt:
      return ">";
    case Binary::kGe:
      return ">=";
    case Binary::kEq:
      return "==";
    case Binary::kNeq:
      return "!=";
    case Binary::kAnd:
      return "&&";
    case Binary::kOr:
      return "||";
    case Binary::kMod:
      return "%";
    case Binary::kCoalesce:
      return "??";
    case Binary::kCompose:
      return ">>";
    case Binary::kComposeBack:
      return "<<";
  }
  return "?";
}

auto N(const Name& name) -> std::string {
  return name.missing ? "<missing>" : name.text;
}

// The part of the line after the node's kind.
auto Details(const Node* node) -> std::string {
  switch (node->GetKind()) {
    case Node::kDirective: {
      const auto* d = node->As<Directive>();
      return "@" + N(d->name) + (d->has_parens ? " (...)" : "");
    }
    case Node::kArg: {
      const auto* a = node->As<Arg>();
      switch (a->form) {
        case Arg::kNamed:
          return "named " + N(a->name);
        case Arg::kSpread:
          return "spread";
        case Arg::kPun:
          return "pun " + N(a->name);
        default:
          return "positional";
      }
    }
    case Node::kParam:
      return N(node->As<Param>()->name);
    case Node::kField: {
      const auto* f = node->As<Field>();
      return N(f->name) + (f->optional ? "?" : "");
    }
    case Node::kTypeDecl: {
      const auto* d = node->As<TypeDecl>();
      std::string out = N(d->name);
      for (size_t idx = 0; idx < d->type_params.size(); idx++)
        out += (idx == 0 ? " <" : ", ") + N(d->type_params[idx]) + (idx + 1 == d->type_params.size() ? ">" : "");
      return out;
    }
    case Node::kFnDecl:
      return N(node->As<FnDecl>()->name);
    case Node::kExternDecl: {
      const auto* d = node->As<ExternDecl>();
      return N(d->c_name) + (d->has_symbol ? " symbol=" + Quote(d->symbol) : "") + (d->variadic ? " variadic" : "");
    }
    case Node::kUseDecl: {
      const auto* d = node->As<UseDecl>();
      return Quote(d->header) + (d->has_alias ? " as " + N(d->alias) : "");
    }
    case Node::kBinding: {
      const auto* b = node->As<Binding>();
      if (b->pattern != nullptr)
        return "let (destructure)";
      return std::string(b->is_let ? "let " : "set ") + N(b->name);
    }
    case Node::kNamedType:
      return N(node->As<NamedType>()->name);
    case Node::kVariantType:
      return N(node->As<VariantType>()->name);
    case Node::kQualifiedType: {
      const auto* t = node->As<QualifiedType>();
      return N(t->qualifier) + "." + N(t->member);
    }
    case Node::kNumberLit:
      return node->As<NumberLit>()->text;
    case Node::kStringLit:
      return Quote(node->As<StringLit>()->value);
    case Node::kBoolLit:
      return node->As<BoolLit>()->value ? "true" : "false";
    case Node::kUnitLit:
      return node->As<UnitLit>()->text;
    case Node::kVarRef:
      return N(node->As<VarRef>()->name);
    case Node::kVariantRef:
      return N(node->As<VariantRef>()->name);
    case Node::kForExpr: {
      const auto* f = node->As<ForExpr>();
      return N(f->first) + (f->second != nullptr ? ", ..." : "") + (f->has_fold ? " fold " + N(f->accumulator) : "") +
             (f->bracketed ? " [comprehension]" : "");
    }
    case Node::kLambda: {
      const auto* l = node->As<Lambda>();
      std::string out = "(";
      for (size_t idx = 0; idx < l->params.size(); idx++)
        out += (idx == 0 ? "" : ", ") + N(l->params[idx]);
      return out + ")";
    }
    case Node::kUiElement:
      return "#" + N(node->As<UiElement>()->tag);
    case Node::kUnary:
      return node->As<Unary>()->op == Unary::kNeg ? "-" : "!";
    case Node::kBinary:
      return OpName(node->As<Binary>()->op);
    case Node::kMember:
      return std::string(node->As<Member>()->optional ? "?." : ".") + N(node->As<Member>()->member);
    case Node::kCall:
      return node->As<Call>()->optional ? "optional" : "";
    case Node::kIndex:
      return node->As<Index>()->optional ? "optional" : "";
    case Node::kRangeExpr: {
      const auto* r = node->As<RangeExpr>();
      return std::string(r->begin == nullptr ? "(no start) " : "") + (r->inclusive ? "..=" : "..") +
             (r->end == nullptr ? " (no end)" : "");
    }
    case Node::kRangePattern: {
      const auto* r = node->As<RangePattern>();
      return std::string(r->begin == nullptr ? "(no start) " : "") + (r->inclusive ? "..=" : "..") +
             (r->end == nullptr ? " (no end)" : "");
    }
    case Node::kStringTemplate: {
      const auto* t = node->As<StringTemplate>();
      std::string out{};
      for (size_t idx = 0; idx < t->texts.size(); idx++)
        out += (idx == 0 ? "" : " {} ") + Quote(t->texts[idx]);
      return out;
    }
    case Node::kSetRule:
      return N(node->As<SetRule>()->tag);
    case Node::kUpdateEntry: {
      const auto* e = node->As<UpdateEntry>();
      std::string out{};
      for (size_t idx = 0; idx < e->path.size(); idx++)
        out += (idx == 0 ? "" : ".") + N(e->path[idx]);
      static const char* kOps[] = {":", "+=", "-=", "*=", "/=", "%="};
      return out + " " + kOps[e->op];
    }
    case Node::kRecordPattern: {
      const auto* r = node->As<RecordPattern>();
      return (r->has_type ? N(r->type_name) : std::string()) + (r->has_rest ? " ...rest" : "");
    }
    case Node::kFieldPattern:
      return N(node->As<FieldPattern>()->name);
    case Node::kBindingPattern:
      return N(node->As<BindingPattern>()->name);
    case Node::kVariantPattern:
      return N(node->As<VariantPattern>()->name);
    case Node::kLiteralPattern:
      return node->As<LiteralPattern>()->negated ? "negated" : "";
    default:
      return {};
  }
}

void Print(std::ostream& out, const Node* node, const int depth, const PrintOptions& options) {
  out << std::string(static_cast<size_t>(depth) * 2, ' ') << node->GetKindName();
  const auto details = Details(node);
  if (!details.empty())
    out << ' ' << details;
  if (options.spans)
    out << ' ' << node->span;
  out << '\n';
  node->ForEachChild([&](const Node* child) {
    Print(out, child, depth + 1, options);
  });
}
}  // namespace

void PrintTree(std::ostream& out, const SyntaxTree& tree, const PrintOptions& options) {
  Print(out, tree.GetRoot(), 0, options);
}

auto PrintTreeToString(const SyntaxTree& tree, const PrintOptions& options) -> std::string {
  std::stringstream ss{};
  PrintTree(ss, tree, options);
  return ss.str();
}

void PrintTokens(std::ostream& out, const SyntaxTree& tree) {
  for (const auto& token : tree.GetTokens()) {
    const auto pos = tree.GetSource().GetPosition(token.span.begin);
    out << pos << '\t' << Token::GetName(token.kind) << '\t' << token.span;
    if (token.kind != Token::kEof)
      out << '\t' << Quote(std::string(tree.GetText(token.span)));
    out << '\n';
  }
}
}  // namespace kura::syntax
