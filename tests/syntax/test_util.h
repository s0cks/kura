#ifndef KURA_TESTS_SYNTAX_TEST_UTIL_H
#define KURA_TESTS_SYNTAX_TEST_UTIL_H

#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "frontend/syntax/ast.h"
#include "frontend/syntax/lexer.h"
#include "frontend/syntax/parser.h"

namespace kura::syntax::test {
inline auto Tree(const std::string& text) -> std::unique_ptr<SyntaxTree> {
  return ParseText("test.kui", text);
}

inline auto ReadFile(const std::string& path) -> std::string {
  std::ifstream in(path, std::ios::binary);
  std::stringstream ss{};
  ss << in.rdbuf();
  return ss.str();
}

inline auto Kinds(const std::string& text) -> std::vector<Token::Kind> {
  DiagnosticList diagnostics{};
  const SourceFile source("test.kui", text);
  std::vector<Token::Kind> kinds{};
  for (const auto& token : Lex(source, diagnostics).tokens)
    kinds.push_back(token.kind);
  return kinds;
}

// A compact S-expression for an expression node, for asserting the shape of
// what was parsed without writing out the whole tree.
inline auto Sexp(const Node* node) -> std::string {
  if (node == nullptr)
    return "<null>";
  const auto list = [](const std::string& head, const std::vector<Node*>& nodes) {
    std::string out = "(" + head;
    for (const auto* n : nodes)
      out += " " + Sexp(n);
    return out + ")";
  };
  switch (node->GetKind()) {
    case Node::kNumberLit:
      return node->As<NumberLit>()->text;
    case Node::kStringLit:
      return "\"" + node->As<StringLit>()->value + "\"";
    case Node::kBoolLit:
      return node->As<BoolLit>()->value ? "true" : "false";
    case Node::kNoneLit:
      return "None";
    case Node::kUnitLit:
      return node->As<UnitLit>()->text;
    case Node::kVarRef:
      return node->As<VarRef>()->name.text;
    case Node::kVariantRef:
      return node->As<VariantRef>()->name.text;
    case Node::kParen:
      return "(paren " + Sexp(node->As<Paren>()->inner) + ")";
    case Node::kUnary: {
      const auto* u = node->As<Unary>();
      return std::string("(") + (u->op == Unary::kNeg ? "neg " : "not ") + Sexp(u->operand) + ")";
    }
    case Node::kBinary: {
      const auto* b = node->As<Binary>();
      static const char* kOps[] = {"+", "-", "*", "/", "<", "<=", ">", ">=", "==", "!=", "&&", "||"};
      return std::string("(") + kOps[b->op] + " " + Sexp(b->lhs) + " " + Sexp(b->rhs) + ")";
    }
    case Node::kMember: {
      const auto* m = node->As<Member>();
      return "(. " + Sexp(m->object) + " " + m->member.text + ")";
    }
    case Node::kCall: {
      const auto* c = node->As<Call>();
      std::string out = "(call " + Sexp(c->callee);
      for (const auto* a : c->args)
        out += " " + Sexp(a);
      return out + ")";
    }
    case Node::kPipe: {
      const auto* p = node->As<Pipe>();
      std::string out = "(pipe " + Sexp(p->lhs) + " " + Sexp(p->callee);
      for (const auto* a : p->args)
        out += " " + Sexp(a);
      return out + ")";
    }
    case Node::kSeqLit:
      return list("seq", node->As<SeqLit>()->elements);
    case Node::kRecordLit: {
      std::string out = "(record";
      for (const auto* e : node->As<RecordLit>()->entries) {
        const auto* arg = e->As<Arg>();
        out += arg->form == Arg::kSpread ? " ..." + Sexp(arg->value) : " " + arg->name.text + ":" + Sexp(arg->value);
      }
      return out + ")";
    }
    case Node::kBlock: {
      const auto* b = node->As<Block>();
      return "(block " + std::to_string(b->statements.size()) + " " + (b->result ? Sexp(b->result) : "-") + ")";
    }
    case Node::kLambda: {
      const auto* l = node->As<Lambda>();
      std::string out = "(lambda";
      for (const auto& p : l->params)
        out += " " + p.text;
      return out + " => " + Sexp(l->body) + ")";
    }
    case Node::kIfExpr: {
      const auto* i = node->As<IfExpr>();
      return "(if " + Sexp(i->condition) + " " + Sexp(i->then_branch) + " " + Sexp(i->else_branch) + ")";
    }
    case Node::kUiElement:
      return "(#" + node->As<UiElement>()->tag.text + ")";
    case Node::kError:
      return "<error>";
    default:
      return std::string("<") + node->GetKindName() + ">";
  }
}

// The value of `let x = <expr>` parsed from `expr`.
inline auto ExprSexp(const std::string& expr) -> std::string {
  const auto tree = Tree("let x = " + expr);
  if (tree->GetRoot()->items.empty())
    return "<no items>";
  const auto* binding = tree->GetRoot()->items[0]->As<Binding>();
  return binding == nullptr ? "<not a binding>" : Sexp(binding->value);
}

inline void CollectNodes(const Node* node, std::vector<const Node*>& out) {
  out.push_back(node);
  node->ForEachChild([&](const Node* child) { CollectNodes(child, out); });
}
}  // namespace kura::syntax::test

#endif  // KURA_TESTS_SYNTAX_TEST_UTIL_H
