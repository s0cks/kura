#ifndef KURA_DOCUMENT_BUILDER_H
#define KURA_DOCUMENT_BUILDER_H

#include <iostream>
#include <rang.hpp>

#include "config.h"
#include "document.h"
#include "frontend/syntax/ast.h"

namespace kura::fmt {

static inline auto FormatName(const syntax::Name& rhs) -> NodePtr {
  return NewText(!rhs.missing ? rhs.text : "");
}

#define LOWER_FN(Name) static inline auto Lower##Name(const Config& config, const syntax::Name& rhs) -> NodePtr

LOWER_FN(SeqLit) {
  std::vector<NodePtr> parts{};

  for (const auto& val : rhs.elements)
    parts.push_back(Lower(config, *val));

  return Enclose("[", "]",
                 Punctuate(NewConcat({
                               NewText(","),
                               NewLine(),
                           }),
                           parts));
}

LOWER_FN(UnitLit) {
  return NewText(rhs.text);
}

LOWER_FN(NumberLit) {
  return NewText(rhs.text);
}

LOWER_FN(Directive) {
  std::vector<NodePtr> parts = {NewText("@"), FormatName(rhs.name)};
  if (rhs.has_parens) {
    std::vector<NodePtr> args{};
    for (auto* arg : rhs.args)
      args.push_back(Lower(config, *arg));

    parts.push_back(Enclose("(", ")",
                            Punctuate(NewConcat({
                                          NewText(","),
                                          NewLine(),
                                      }),
                                      args)));
  }

  return NewConcat(parts);
}

LOWER_FN(Module) {
  // use expressions first
  std::vector<NodePtr> use{};
  for (const auto& item : rhs.items) {
    if (const auto* use_node = item->As<syntax::UseDecl>())
      use.push_back(Lower(config, *use_node));
  }
  const auto use_node = use.empty() ? NewEmpty()
                                    : NewConcat({
                                          Punctuate(NewLine(true), use),
                                          NewLine(true),
                                          NewLine(true),
                                      });

  std::vector<NodePtr> items{};
  if (config.types_first) {
    for (const auto* item : rhs.items) {
      if (const auto* use_node = item->As<syntax::UseDecl>())
        continue;
      else if (const auto* type_node = item->As<syntax::TypeDecl>())
        items.push_back(Lower(config, *item));
    }

    for (const auto* item : rhs.items) {
      if (const auto* use_node = item->As<syntax::UseDecl>())
        continue;
      else if (const auto* type_node = item->As<syntax::TypeDecl>())
        continue;

      items.push_back(Lower(config, *item));
    }
  } else {
    for (const auto* item : rhs.items) {
      if (const auto* use_node = item->As<syntax::UseDecl>())
        continue;

      items.push_back(Lower(config, *item));
    }
  }

  return NewConcat({
      use_node,
      Punctuate(NewConcat({
                    NewLine(true),
                    NewLine(true),
                }),
                items),
  });
}

LOWER_FN(Binding) {
  std::vector<NodePtr> parts{};
  for (const auto* dir : rhs.directives) {
    parts.push_back(Lower(config, *dir));
    parts.push_back(NewLine(true));
  }

  if (rhs.is_let)
    parts.push_back(NewText("let "));

  parts.push_back(FormatName(rhs.name));

  if (rhs.annotation) {
    parts.push_back(NewText(": "));
    parts.push_back(FormatName(rhs.annotation->As<syntax::NamedType>()->name));
  }

  if (rhs.value) {
    parts.push_back(NewText(" = "));
    parts.push_back(Lower(config, *rhs.value));
  }

  return NewGroup(NewConcat(parts));
}

LOWER_FN(Member) {
  std::vector<NodePtr> parts{};

  if (rhs.object) {
    parts.push_back(Lower(config, *rhs.object));
    parts.push_back(NewText("."));
  }

  parts.push_back(FormatName(rhs.member));

  return NewGroup(NewConcat(parts));
}

LOWER_FN(Call) {
  std::vector<NodePtr> args{};
  for (const auto& arg : rhs.args)
    args.push_back(Lower(config, *arg));

  return NewGroup(NewConcat({Lower(config, *rhs.callee), Enclose("(", ")",
                                                                 Punctuate(NewConcat({
                                                                               NewText(","),
                                                                               NewLine(),
                                                                           }),
                                                                           args))}));
}

LOWER_FN(Unary) {
  std::vector<NodePtr> parts{};

  switch (rhs.op) {
    case syntax::Unary::kNeg:
      parts.push_back(NewText("-"));
      break;
    case kura::syntax::Unary::kNot:
      parts.push_back(NewText("!"));
      break;
  }
  parts.push_back(Lower(config, *rhs.operand));

  return NewConcat(parts);
}

LOWER_FN(Binary) {
  std::vector<NodePtr> parts{};
  parts.push_back(Lower(config, *rhs.lhs));
  parts.push_back(NewLine());

  switch (rhs.op) {
    case kura::syntax::Binary::kAdd:
      parts.push_back(NewText(" + "));
      break;
    case kura::syntax::Binary::kSub:
      parts.push_back(NewText(" - "));
      break;
    case kura::syntax::Binary::kMul:
      parts.push_back(NewText(" * "));
      break;
    case kura::syntax::Binary::kDiv:
      parts.push_back(NewText(" / "));
      break;
    case kura::syntax::Binary::kEq:
      parts.push_back(NewText(" == "));
      break;
    case kura::syntax::Binary::kNeq:
      parts.push_back(NewText(" != "));
      break;
    case kura::syntax::Binary::kAnd:
      parts.push_back(NewText(" && "));
      break;
    case kura::syntax::Binary::kOr:
      parts.push_back(NewText(" || "));
      break;
    case kura::syntax::Binary::kMod:
      parts.push_back(NewText(" % "));
      break;
    case kura::syntax::Binary::kCoalesce:
      parts.push_back(NewText(" ?? "));
      break;
    case kura::syntax::Binary::kCompose:
      parts.push_back(NewText(" >> "));
      break;
    case kura::syntax::Binary::kComposeBack:
      parts.push_back(NewText(" << "));
      break;
    case kura::syntax::Binary::kLt:
      parts.push_back(NewText(" < "));
      break;
    case kura::syntax::Binary::kLe:
      parts.push_back(NewText(" =< "));
      break;
    case kura::syntax::Binary::kGt:
      parts.push_back(NewText(" > "));
      break;
    case kura::syntax::Binary::kGe:
      parts.push_back(NewText(" >= "));
      break;
    default:
      std::cerr << "invalid binary op: " << rhs.op << std::endl;
      break;
  }

  parts.push_back(NewLine());
  parts.push_back(Lower(config, *rhs.rhs));

  return NewGroup(NewConcat(parts));
}

LOWER_FN(MatchArm) {
  std::vector<NodePtr> parts{};
  parts.push_back(Lower(config, *rhs.pattern));

  if (rhs.guard)
    parts.push_back(Lower(config, *rhs.guard));

  parts.push_back(NewText(" => "));

  parts.push_back(Lower(config, *rhs.body));

  return NewGroup(NewConcat(parts));
}

LOWER_FN(NonePattern) {
  return NewText("None");
}

LOWER_FN(WildcardPattern) {
  return NewText("_");
}

LOWER_FN(LiteralPattern) {
  std::vector<NodePtr> parts{};
  if (rhs.negated)
    parts.push_back(NewText("-"));
  parts.push_back(Lower(config, *rhs.literal));
  return NewConcat(parts);
}

LOWER_FN(MatchExpr) {
  std::vector<NodePtr> arms{};
  for (const auto& arm : rhs.arms)
    arms.push_back(Lower(config, *arm));

  return NewGroup(NewConcat({
      NewText("match "),
      Lower(config, *rhs.scrutinee),
      NewText(" "),
      Enclose("{", "}", Punctuate(NewConcat({NewText(", "), NewLine()}), arms)),
  }));
}

LOWER_FN(RecordLit) {
  std::vector<NodePtr> fields{};

  for (const auto& prop : rhs.entries)
    fields.push_back(Lower(config, *prop));

  const auto inner = NewConcat({
      Punctuate(NewConcat({
                    NewText(", "),
                    NewLine(),
                }),
                fields),
      NewText(", "),
  });

  return NewGroup(Enclose("{", "}", inner));
}

LOWER_FN(Arg) {
  std::vector<NodePtr> parts{};

  switch (rhs.form) {
    case kura::syntax::Arg::kSpread:
      parts.push_back(NewText("..."));
      break;
    case kura::syntax::Arg::kNamed:
      parts.push_back(FormatName(rhs.name));
      if (rhs.value) {
        parts.push_back(NewText(": "));
        parts.push_back(Lower(config, *rhs.value));
      }
      break;
    case kura::syntax::Arg::kPun:
    case kura::syntax::Arg::kPositional:
      parts.push_back(Lower(config, *rhs.value));
      break;
  }

  return NewGroup(NewConcat(parts));
}

LOWER_FN(Param) {
  std::vector<NodePtr> parts{};

  parts.push_back(FormatName(rhs.name));
  parts.push_back(NewText(": "));
  parts.push_back(Lower(config, *rhs.type));

  return NewGroup(NewConcat(parts));
}

LOWER_FN(VarRef) {
  return FormatName(rhs.name);
}

LOWER_FN(FnDecl) {
  std::vector<NodePtr> parts{};
  parts.push_back(NewText("fn "));

  for (const auto& dir : rhs.directives)
    parts.push_back(Lower(config, *dir));

  parts.push_back(FormatName(rhs.name));

  std::vector<NodePtr> args{};
  for (const auto& arg : rhs.params)
    args.push_back(Lower(config, *arg));

  parts.push_back(Enclose("(", ")",
                          Punctuate(NewConcat({
                                        NewText(", "),
                                    }),
                                    args),
                          0, false));

  if (rhs.result) {
    parts.push_back(NewText(": "));
    parts.push_back(Lower(config, *rhs.result));
  }

  parts.push_back(NewText(" => "));
  parts.push_back(NewIndent(2, NewConcat({
                                   NewLine(),
                                   Lower(config, *rhs.body),
                               })));

  return NewGroup(NewConcat(parts));
}

LOWER_FN(UseDecl) {
  std::vector<NodePtr> parts{};

  for (const auto& dir : rhs.directives)
    parts.push_back(Lower(config, *dir));

  parts.push_back(NewText("use"));
  parts.push_back(NewLine());
  parts.push_back(DoubleQuote(NewText(rhs.header)));

  if (rhs.has_alias) {
    parts.push_back(NewLine());
    parts.push_back(NewText("as"));
    parts.push_back(NewLine());
    parts.push_back(FormatName(rhs.alias));
  }

  return NewGroup(NewConcat(parts));
}

LOWER_FN(NamedType) {
  return FormatName(rhs.name);
}

LOWER_FN(NoneType) {
  return NewText("None");
}

LOWER_FN(NoneLit) {
  return NewText("None");
}

LOWER_FN(StringLit) {
  return DoubleQuote(NewText(rhs.value));
}

LOWER_FN(VariantType) {
  std::vector<NodePtr> parts{};
  parts.push_back(FormatName(rhs.name));

  if (rhs.payload)
    parts.push_back(Enclose("(", ")", Lower(config, *rhs.payload), 0, false));

  return NewGroup(NewConcat(parts));
}

LOWER_FN(UnionType) {
  std::vector<NodePtr> parts{};
  for (const auto& type : rhs.members)
    parts.push_back(Lower(config, *type));

  return NewGroup(Punctuate(NewConcat({NewLine(), NewText("|"), NewLine()}), parts));
}

LOWER_FN(FunctionType) {
  std::vector<NodePtr> parts{};

  std::vector<NodePtr> params{};
  for (const auto& param : rhs.params) {
    params.push_back(Lower(config, *param));
  }

  parts.push_back(Enclose("(", ")", Punctuate(NewConcat({NewText(","), NewLine()}), params), 0, false));
  parts.push_back(NewLine());
  parts.push_back(NewText("=>"));
  parts.push_back(NewLine());
  parts.push_back(Lower(config, *rhs.result));

  return NewGroup(NewConcat(parts));
}

LOWER_FN(Field) {
  std::vector<NodePtr> parts{};
  parts.push_back(FormatName(rhs.name));
  if (rhs.optional)
    parts.push_back(NewText("?"));
  parts.push_back(NewText(": "));
  parts.push_back(Lower(config, *rhs.type));
  return NewGroup(NewConcat(parts));
}

LOWER_FN(RecordType) {
  std::vector<NodePtr> parts{};
  for (const auto& field : rhs.fields)
    parts.push_back(Lower(config, *field));

  return Enclose("{", "}", Punctuate(NewConcat({NewText(","), NewLine()}), parts), 2, true, true, true);
}

LOWER_FN(TypeDecl) {
  std::vector<NodePtr> parts{};

  parts.push_back(NewText("type "));
  parts.push_back(FormatName(rhs.name));

  if (!rhs.type_params.empty()) {
    std::vector<NodePtr> type_params{};
    for (const auto& type : rhs.type_params)
      type_params.push_back(FormatName(type));

    parts.push_back(Enclose("<", ">",
                            Punctuate(NewConcat({
                                          NewText(","),
                                      }),
                                      type_params),
                            0, false));
  }

  parts.push_back(NewText(" = "));

  if (rhs.type)
    parts.push_back(Lower(config, *rhs.type));

  return NewGroup(NewConcat(parts));
}

auto Lower(const Config& config, const syntax::Node& node) -> NodePtr {
  switch (node.GetKind()) {
#define LOWER(Name)           \
  case syntax::Node::k##Name: \
    return Lower##Name(config, *node.As<syntax::Name>());

    LOWER(Directive);
    LOWER(Module);
    LOWER(Binding);
    LOWER(UseDecl);
    LOWER(FnDecl);
    LOWER(UnitLit);
    LOWER(NumberLit);
    LOWER(NoneLit);
    LOWER(SeqLit);
    LOWER(StringLit);
    LOWER(RecordLit);
    LOWER(TypeDecl);
    LOWER(VariantType);
    LOWER(RecordType);
    LOWER(UnionType);
    LOWER(NamedType);
    LOWER(FunctionType);
    LOWER(NoneType);
    LOWER(Field);
    LOWER(Call);
    LOWER(Param);
    LOWER(VarRef);
    LOWER(Member);
    LOWER(Binary);
    LOWER(Unary);
    LOWER(Arg);
    LOWER(MatchExpr);
    LOWER(MatchArm);
    LOWER(LiteralPattern);
    LOWER(NonePattern);
    LOWER(WildcardPattern);

#undef LOWER
    default:
      std::cerr << rang::style::bold << rang::fg::red << "[error]" << rang::style::reset << " ";
      std::cerr << "invalid node kind: " << node.GetKindName();
      std::cerr << std::endl;
      return NewEmpty();
  }
}

#undef LOWER_FN
}  // namespace kura::fmt

#endif  // KURA_DOCUMENT_BUILDER_H
