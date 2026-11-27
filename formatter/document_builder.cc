#include "formatter/document_builder.h"

#include <iostream>
#include <rang.hpp>

#include "compiler/frontend/syntax/ast.h"
#include "formatter/config.h"
#include "formatter/document.h"

namespace kura::fmt {
static inline auto FormatName(const syntax::Name& rhs) -> NodePtr {
  return NewText(!rhs.missing ? rhs.text : "");
}

static inline bool IsBracedBlock(const syntax::Node* node) {
  return node && (node->Is<syntax::Block>() || node->Is<syntax::RecordType>() || node->Is<syntax::RecordLit>() ||
                  node->Is<syntax::RecordUpdate>() || node->Is<syntax::MatchExpr>());
}

#define LOWER_FN(Name) auto DocumentBuilder::Lower##Name(const syntax::Name& rhs)->NodePtr

LOWER_FN(MatchArm) {
  std::vector<NodePtr> parts{};
  parts.push_back(Lower(*rhs.pattern));

  if (rhs.guard) {
    parts.push_back(NewText(" if "));
    parts.push_back(Lower(*rhs.guard));
  }

  parts.push_back(NewText(" =>"));

  if (IsBracedBlock(rhs.body)) {
    parts.push_back(NewText(" "));
    parts.push_back(Lower(*rhs.body));
  } else {
    parts.push_back(NewBreakIf(NewEmpty(), NewText(" ")));
    parts.push_back(NewIndent(GetTabWidth(), NewConcat({
                                                 NewSoftLine(),
                                                 Lower(*rhs.body),
                                             })));
  }

  return NewGroup(NewConcat(parts));
}

LOWER_FN(MatchExpr) {
  if (rhs.arms.empty())
    return NewConcat({NewText("match "), Lower(*rhs.scrutinee), NewText(" {}")});

  std::vector<NodePtr> arms{};
  for (const auto& arm : rhs.arms)
    arms.push_back(Lower(*arm));

  return NewConcat({
      NewText("match "),
      Lower(*rhs.scrutinee),
      NewText(" {"),
      NewIndent(GetTabWidth(), NewConcat({
                                   NewHardLine(),
                                   Punctuate(NewConcat({NewText(","), NewHardLine()}), arms),
                                   NewText(","),
                               })),
      NewHardLine(),
      NewText("}"),
  });
}

LOWER_FN(UiElement) {
  std::vector<NodePtr> parts{};

  for (const auto& dir : rhs.directives) {
    parts.push_back(NewConcat({
        Lower(*dir),
        NewHardLine(),
    }));
  }

  parts.push_back(NewText("#"));
  parts.push_back(FormatName(rhs.tag));

  if (rhs.has_props) {
    std::vector<NodePtr> props{};
    if (rhs.has_id)
      props.push_back(NewConcat({NewText(":"), FormatName(rhs.id)}));
    for (const auto& prop : rhs.props)
      props.push_back(Lower(*prop));

    if (props.empty()) {
      parts.push_back(NewText("()"));
    } else {
      parts.push_back(NewGroup(NewConcat({
          NewText("("),
          NewIndent(GetTabWidth(), NewConcat({
                                       NewSoftLine(),
                                       Punctuate(NewConcat({NewText(","), NewLine()}), props),
                                       NewBreakIf(NewText(","), NewEmpty()),
                                   })),
          NewSoftLine(),
          NewText(")"),
      })));
    }
  }

  if (rhs.has_children) {
    std::vector<NodePtr> children{};
    for (const auto& child : rhs.children)
      children.push_back(Lower(*child));

    if (children.empty()) {
      parts.push_back(NewText(" {}"));
    } else {
      parts.push_back(NewConcat({
          NewText(" {"),
          NewIndent(GetTabWidth(), NewConcat({
                                       NewHardLine(),
                                       Punctuate(NewConcat({
                                                     NewHardLine(),
                                                     NewHardLine(),
                                                 }),
                                                 children),
                                   })),
          NewHardLine(),
          NewText("}"),
      }));
    }
  }

  return NewGroup(NewConcat(parts));
}

LOWER_FN(RecordType) {
  if (rhs.fields.empty())
    return NewText("{}");

  std::vector<NodePtr> parts{};
  for (const auto& field : rhs.fields)
    parts.push_back(Lower(*field));

  return NewConcat({
      NewText("{"),
      NewIndent(GetTabWidth(), NewConcat({
                                   NewHardLine(),
                                   Punctuate(NewConcat({NewText(","), NewHardLine()}), parts),
                                   NewText(","),
                               })),
      NewHardLine(),
      NewText("}"),
  });
}

LOWER_FN(FnDecl) {
  std::vector<NodePtr> parts{};

  for (const auto& dir : rhs.directives)
    parts.push_back(NewConcat({
        Lower(*dir),
        NewHardLine(),
    }));

  parts.push_back(NewText("fn "));
  parts.push_back(FormatName(rhs.name));

  std::vector<NodePtr> args{};
  for (const auto& arg : rhs.params)
    args.push_back(Lower(*arg));

  if (args.empty()) {
    parts.push_back(NewText("()"));
  } else {
    parts.push_back(NewGroup(NewConcat({
        NewText("("),
        NewIndent(GetTabWidth(), NewConcat({
                                     NewSoftLine(),
                                     Punctuate(NewConcat({NewText(","), NewLine()}), args),
                                     NewBreakIf(NewText(","), NewEmpty()),
                                 })),
        NewSoftLine(),
        NewText(")"),
    })));
  }

  if (rhs.result) {
    parts.push_back(NewText(": "));
    parts.push_back(Lower(*rhs.result));
  }

  parts.push_back(NewText(" =>"));

  if (IsBracedBlock(rhs.body)) {
    parts.push_back(NewText(" "));
    parts.push_back(Lower(*rhs.body));
  } else {
    parts.push_back(NewBreakIf(NewEmpty(), NewText(" ")));
    parts.push_back(NewIndent(GetTabWidth(), NewConcat({
                                                 NewSoftLine(),
                                                 Lower(*rhs.body),
                                             })));
  }

  return NewGroup(NewConcat(parts));
}

LOWER_FN(SeqLit) {
  std::vector<NodePtr> parts{};

  for (const auto& val : rhs.elements)
    parts.push_back(Lower(*val));

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
      args.push_back(Lower(*arg));

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
      use.push_back(Lower(*use_node));
  }
  const auto use_node = use.empty() ? NewEmpty()
                                    : NewConcat({
                                          Punctuate(NewLine(true, true), use),
                                          NewLine(true, true),
                                          NewLine(true, true),
                                      });

  std::vector<NodePtr> items{};
  if (config_.types_first) {
    for (const auto* item : rhs.items) {
      if (const auto* use_node = item->As<syntax::UseDecl>())
        continue;
      else if (const auto* type_node = item->As<syntax::TypeDecl>())
        items.push_back(Lower(*item));
    }

    for (const auto* item : rhs.items) {
      if (const auto* use_node = item->As<syntax::UseDecl>())
        continue;
      else if (const auto* type_node = item->As<syntax::TypeDecl>())
        continue;

      items.push_back(Lower(*item));
    }
  } else {
    for (const auto* item : rhs.items) {
      if (const auto* use_node = item->As<syntax::UseDecl>())
        continue;

      items.push_back(Lower(*item));
    }
  }

  return NewConcat({
      use_node,
      Punctuate(NewConcat({
                    NewHardLine(),
                    NewHardLine(),
                }),
                items),
  });
}

LOWER_FN(Binding) {
  std::vector<NodePtr> parts{};
  for (const auto* dir : rhs.directives) {
    parts.push_back(Lower(*dir));
    parts.push_back(NewLine(true, true));
  }

  if (rhs.is_let)
    parts.push_back(NewText("let "));

  // `let { a, b } = r` destructures: there is a pattern where the name would be.
  if (rhs.pattern)
    parts.push_back(Lower(*rhs.pattern));
  else
    parts.push_back(FormatName(rhs.name));

  if (rhs.annotation) {
    parts.push_back(NewText(": "));
    parts.push_back(Lower(*rhs.annotation));
  }

  if (rhs.value) {
    parts.push_back(NewText(" ="));
    parts.push_back(NewIndent(GetTabWidth(), NewConcat({
                                                 NewLine(),
                                                 Lower(*rhs.value),
                                             })));
  }

  return NewGroup(NewConcat(parts));
}

LOWER_FN(Member) {
  std::vector<NodePtr> parts{};

  if (rhs.object) {
    parts.push_back(Lower(*rhs.object));
    parts.push_back(NewText("."));
  }

  parts.push_back(FormatName(rhs.member));

  return NewGroup(NewConcat(parts));
}

LOWER_FN(Call) {
  std::vector<NodePtr> args{};
  for (const auto& arg : rhs.args)
    args.push_back(Lower(*arg));

  return NewGroup(NewConcat({
      Lower(*rhs.callee),
      NewText("("),
      NewIndent(GetTabWidth(), NewConcat({
                                   NewSoftLine(),
                                   Punctuate(NewConcat({
                                                 NewText(","),
                                                 NewLine(),
                                             }),
                                             args),
                               })),
      NewSoftLine(),
      NewText(")"),
  }));
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
  parts.push_back(Lower(*rhs.operand));

  return NewConcat(parts);
}

LOWER_FN(Binary) {
  std::string op_str;
  switch (rhs.op) {
    case kura::syntax::Binary::kAdd:
      op_str = "+";
      break;
    case kura::syntax::Binary::kSub:
      op_str = "-";
      break;
    case kura::syntax::Binary::kMul:
      op_str = "*";
      break;
    case kura::syntax::Binary::kDiv:
      op_str = "/";
      break;
    case kura::syntax::Binary::kEq:
      op_str = "==";
      break;
    case kura::syntax::Binary::kNeq:
      op_str = "!=";
      break;
    case kura::syntax::Binary::kAnd:
      op_str = "&&";
      break;
    case kura::syntax::Binary::kOr:
      op_str = "||";
      break;
    case kura::syntax::Binary::kMod:
      op_str = "%";
      break;
    case kura::syntax::Binary::kCoalesce:
      op_str = "??";
      break;
    case kura::syntax::Binary::kCompose:
      op_str = ">>";
      break;
    case kura::syntax::Binary::kComposeBack:
      op_str = "<<";
      break;
    case kura::syntax::Binary::kLt:
      op_str = "<";
      break;
    case kura::syntax::Binary::kLe:
      op_str = "=<";
      break;
    case kura::syntax::Binary::kGt:
      op_str = ">";
      break;
    case kura::syntax::Binary::kGe:
      op_str = ">=";
      break;
    default:
      std::cerr << "invalid binary op: " << rhs.op << std::endl;
      break;
  }

  return NewGroup(NewConcat({
      Lower(*rhs.lhs),
      NewIndent(GetTabWidth(), NewConcat({
                                   NewLine(),
                                   NewText(op_str),
                                   NewText(" "),
                                   Lower(*rhs.rhs),
                               })),
  }));
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
  parts.push_back(Lower(*rhs.literal));
  return NewConcat(parts);
}

LOWER_FN(RecordLit) {
  if (rhs.entries.empty())
    return NewText("{}");

  std::vector<NodePtr> fields{};
  for (const auto& prop : rhs.entries)
    fields.push_back(Lower(*prop));

  return NewGroup(NewConcat({
      NewText("{"),
      NewBreakIf(NewEmpty(), NewText(" ")),
      NewIndent(GetTabWidth(), NewConcat({
                                   NewSoftLine(),
                                   Punctuate(NewConcat({NewText(","), NewLine()}), fields),
                                   NewBreakIf(NewText(","), NewEmpty()),
                               })),
      NewSoftLine(),
      NewBreakIf(NewEmpty(), NewText(" ")),
      NewText("}"),
  }));
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
        parts.push_back(Lower(*rhs.value));
      }
      break;
    case kura::syntax::Arg::kPun:
      // `width:`, the name and nothing after: the value is the one the parser made up
      parts.push_back(FormatName(rhs.name));
      parts.push_back(NewText(":"));
      break;
    case kura::syntax::Arg::kPositional:
      parts.push_back(Lower(*rhs.value));
      break;
  }

  return NewGroup(NewConcat(parts));
}

LOWER_FN(Param) {
  std::vector<NodePtr> parts{};

  parts.push_back(FormatName(rhs.name));
  parts.push_back(NewText(": "));
  parts.push_back(Lower(*rhs.type));

  return NewGroup(NewConcat(parts));
}

LOWER_FN(VarRef) {
  return FormatName(rhs.name);
}

LOWER_FN(Placeholder) {
  return NewText("_");
}

LOWER_FN(Pipe) {
  std::vector<NodePtr> parts{};

  if (rhs.lhs)
    parts.push_back(Lower(*rhs.lhs));

  if (rhs.stage)
    parts.push_back(NewIndent(GetTabWidth(), NewConcat({
                                                 NewLine(),
                                                 NewText("|>"),
                                                 NewText(" "),
                                                 Lower(*rhs.stage),
                                             })));

  return NewGroup(NewConcat(parts));
}

LOWER_FN(BoolLit) {
  return NewText(rhs.value ? "true" : "false");
}

LOWER_FN(Index) {
  return NewConcat({
      rhs.object ? Lower(*rhs.object) : NewEmpty(),
      NewText(rhs.optional ? "?[" : "["),
      rhs.index ? Lower(*rhs.index) : NewEmpty(),
      NewText("]"),
  });
}

LOWER_FN(RangeExpr) {
  return NewConcat({
      rhs.begin ? Lower(*rhs.begin) : NewEmpty(),
      NewText(rhs.inclusive ? "..=" : ".."),
      rhs.end ? Lower(*rhs.end) : NewEmpty(),
  });
}

LOWER_FN(RangePattern) {
  return NewConcat({
      rhs.begin ? Lower(*rhs.begin) : NewEmpty(),
      NewText(rhs.inclusive ? "..=" : ".."),
      rhs.end ? Lower(*rhs.end) : NewEmpty(),
  });
}

LOWER_FN(FieldPattern) {
  if (!rhs.pattern)
    return FormatName(rhs.name);
  return NewConcat({FormatName(rhs.name), NewText(": "), Lower(*rhs.pattern)});
}

LOWER_FN(RecordPattern) {
  std::vector<NodePtr> items{};
  for (const auto* field : rhs.fields)
    items.push_back(Lower(*field));
  if (rhs.has_rest)
    items.push_back(NewText(rhs.rest.missing ? "..." : "..." + rhs.rest.text));

  std::vector<NodePtr> parts{};
  if (rhs.has_type) {
    parts.push_back(FormatName(rhs.type_name));
    parts.push_back(NewText(" "));
  }
  parts.push_back(Enclose("{", "}", Punctuate(NewConcat({NewText(","), NewLine()}), items)));
  return NewGroup(NewConcat(parts));
}

LOWER_FN(QualifiedType) {
  std::vector<NodePtr> parts{};
  parts.push_back(FormatName(rhs.qualifier));
  parts.push_back(NewText("."));
  parts.push_back(FormatName(rhs.member));
  if (!rhs.args.empty()) {
    std::vector<NodePtr> args{};
    for (const auto* arg : rhs.args)
      args.push_back(Lower(*arg));
    parts.push_back(NewText("<"));
    parts.push_back(Punctuate(NewText(", "), args));
    parts.push_back(NewText(">"));
  }
  return NewConcat(parts);
}

LOWER_FN(StaticAssert) {
  std::vector<NodePtr> parts{};
  parts.push_back(NewText("static-assert("));
  if (rhs.condition)
    parts.push_back(Lower(*rhs.condition));
  if (rhs.message) {
    parts.push_back(NewText(", "));
    parts.push_back(Lower(*rhs.message));
  }
  parts.push_back(NewText(")"));
  return NewConcat(parts);
}

static inline auto EscapeString(const std::string& text) -> std::string {
  std::string out{};
  for (const auto c : text) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out += c;
        break;
    }
  }
  return out;
}

LOWER_FN(TestDecl) {
  std::vector<NodePtr> parts{};
  for (const auto& dir : rhs.directives) {
    parts.push_back(NewConcat({
        Lower(*dir),
        NewHardLine(),
    }));
  }
  parts.push_back(NewText("test \"" + EscapeString(rhs.name) + "\" "));
  if (rhs.body)
    parts.push_back(Lower(*rhs.body));
  return NewGroup(NewConcat(parts));
}

LOWER_FN(ExternDecl) {
  std::vector<NodePtr> parts{};
  for (const auto& dir : rhs.directives) {
    parts.push_back(NewConcat({
        Lower(*dir),
        NewHardLine(),
    }));
  }

  parts.push_back(NewText("extern "));
  if (rhs.has_symbol)
    parts.push_back(NewText("\"" + EscapeString(rhs.symbol) + "\" "));
  parts.push_back(NewText("fn "));
  parts.push_back(FormatName(rhs.c_name));

  std::vector<NodePtr> args{};
  for (const auto* param : rhs.params)
    args.push_back(Lower(*param));
  if (rhs.variadic)
    args.push_back(NewText("..."));
  parts.push_back(Enclose("(", ")", Punctuate(NewText(", "), args)));

  if (rhs.result) {
    parts.push_back(NewText(": "));
    parts.push_back(Lower(*rhs.result));
  }

  if (rhs.body) {
    parts.push_back(NewText(" =>"));
    if (IsBracedBlock(rhs.body)) {
      parts.push_back(NewText(" "));
      parts.push_back(Lower(*rhs.body));
    } else {
      parts.push_back(NewText(" "));
      parts.push_back(Lower(*rhs.body));
    }
  }
  return NewGroup(NewConcat(parts));
}

LOWER_FN(Paren) {
  return NewConcat({
      NewText("("),
      rhs.inner ? Lower(*rhs.inner) : NewEmpty(),
      NewText(")"),
  });
}

LOWER_FN(IsExpr) {
  std::vector<NodePtr> parts{};
  if (rhs.value)
    parts.push_back(Lower(*rhs.value));
  parts.push_back(NewText(" is "));
  if (rhs.pattern)
    parts.push_back(Lower(*rhs.pattern));
  return NewGroup(NewConcat(parts));
}

LOWER_FN(ForExpr) {
  std::vector<NodePtr> parts{};
  // `for i in xs` has only the index; `for i, x in xs` has the element too.
  const auto head = [&]() {
    parts.push_back(FormatName(rhs.first));
    if (rhs.second) {
      parts.push_back(NewText(", "));
      parts.push_back(Lower(*rhs.second));
    }
    parts.push_back(NewText(" in "));
    parts.push_back(Lower(*rhs.iterable));
    if (rhs.guard) {
      parts.push_back(NewText(" if "));
      parts.push_back(Lower(*rhs.guard));
    }
  };

  // `[ body for _, x in xs ]` is a comprehension: the body comes first and there is no `=>`.
  if (rhs.bracketed) {
    parts.push_back(NewText("[ "));
    parts.push_back(Lower(*rhs.body));
    parts.push_back(NewText(" for "));
    head();
    parts.push_back(NewText(" ]"));
    return NewGroup(NewConcat(parts));
  }

  parts.push_back(NewText("for "));
  head();

  if (rhs.has_fold) {
    parts.push_back(NewText(" fold "));
    parts.push_back(FormatName(rhs.accumulator));
    parts.push_back(NewText(" = "));
    parts.push_back(Lower(*rhs.initial));
  }

  parts.push_back(NewText(" =>"));
  parts.push_back(NewBreakIf(NewEmpty(), NewText(" ")));
  parts.push_back(NewIndent(GetTabWidth(), NewConcat({
                                               NewSoftLine(),
                                               Lower(*rhs.body),
                                           })));

  return NewGroup(NewConcat(parts));
}

LOWER_FN(VariantPattern) {
  std::vector<NodePtr> parts{};
  parts.push_back(FormatName(rhs.name));

  if (rhs.payload) {
    parts.push_back(NewConcat({
        NewText("("),
        Lower(*rhs.payload),
        NewText(")"),
    }));
  }

  return NewGroup(NewConcat(parts));
}

LOWER_FN(VariantRef) {
  return FormatName(rhs.name);
}

LOWER_FN(Lambda) {
  std::vector<NodePtr> parts{};

  std::vector<NodePtr> args{};
  for (const auto& arg : rhs.params)
    args.push_back(FormatName(arg));

  if (args.empty()) {
    parts.push_back(NewText("()"));
  } else {
    parts.push_back(NewGroup(NewConcat({
        NewText("("),
        NewIndent(GetTabWidth(), NewConcat({
                                     NewSoftLine(),
                                     Punctuate(NewConcat({NewText(","), NewLine()}), args),
                                     NewBreakIf(NewText(","), NewEmpty()),
                                 })),
        NewSoftLine(),
        NewText(")"),
    })));
  }

  parts.push_back(NewText(" =>"));

  if (rhs.body && IsBracedBlock(rhs.body)) {
    parts.push_back(NewText(" "));
    parts.push_back(Lower(*rhs.body));
  } else if (rhs.body) {
    parts.push_back(NewBreakIf(NewEmpty(), NewText(" ")));
    parts.push_back(NewIndent(GetTabWidth(), NewConcat({
                                                 NewSoftLine(),
                                                 Lower(*rhs.body),
                                             })));
  }

  return NewGroup(NewConcat(parts));
}

LOWER_FN(Block) {
  std::vector<NodePtr> parts{};

  uint64_t num_sets = 0;
  for (const auto& stmt : rhs.statements) {
    if (const auto* set = stmt->As<syntax::SetRule>()) {
      parts.push_back(Lower(*stmt));
      num_sets++;
    }

    continue;
  }

  if (num_sets > 0)
    parts.push_back(NewEmpty());

  for (const auto& stmt : rhs.statements) {
    if (const auto* set = stmt->As<syntax::SetRule>())
      continue;

    parts.push_back(Lower(*stmt));
  }

  if (rhs.result)
    parts.push_back(Lower(*rhs.result));

  if (parts.empty())
    return NewText("{}");

  return NewGroup(NewConcat({
      NewText("{"),
      NewIndent(GetTabWidth(), NewConcat({
                                   NewHardLine(),
                                   Punctuate(NewConcat({
                                                 NewHardLine(),
                                             }),
                                             parts),
                               })),
      NewHardLine(),
      NewText("}"),
  }));
}

LOWER_FN(UseDecl) {
  std::vector<NodePtr> parts{};

  for (const auto& dir : rhs.directives)
    parts.push_back(Lower(*dir));

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
  if (rhs.args.empty())
    return FormatName(rhs.name);

  std::vector<NodePtr> type_params{};
  for (const auto& type : rhs.args)
    type_params.push_back(Lower(*type));

  return NewGroup(NewConcat({
      FormatName(rhs.name),
      Enclose("<", ">",
              Punctuate(NewConcat({
                            NewText(","),
                        }),
                        type_params),
              0, false),
  }));
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
    parts.push_back(Enclose("(", ")", Lower(*rhs.payload), 0, false));

  return NewGroup(NewConcat(parts));
}

LOWER_FN(UnionType) {
  std::vector<NodePtr> parts{};
  for (const auto& type : rhs.members)
    parts.push_back(Lower(*type));

  return NewGroup(Punctuate(NewConcat({NewLine(), NewIndent(GetTabWidth(), NewText(" | "))}), parts));
}

LOWER_FN(FunctionType) {
  std::vector<NodePtr> parts{};

  std::vector<NodePtr> params{};
  for (const auto& param : rhs.params) {
    params.push_back(Lower(*param));
  }

  parts.push_back(Enclose("(", ")", Punctuate(NewConcat({NewText(","), NewLine()}), params), 0, false));
  parts.push_back(NewLine());
  parts.push_back(NewText("=>"));
  parts.push_back(NewLine());
  parts.push_back(Lower(*rhs.result));

  return NewGroup(NewConcat(parts));
}

LOWER_FN(Field) {
  std::vector<NodePtr> parts{};
  parts.push_back(FormatName(rhs.name));
  if (rhs.optional)
    parts.push_back(NewText("?"));
  parts.push_back(NewText(": "));
  parts.push_back(Lower(*rhs.type));
  return NewGroup(NewConcat(parts));
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
    parts.push_back(Lower(*rhs.type));

  return NewGroup(NewConcat(parts));
}

LOWER_FN(WhenExpr) {
  std::vector<NodePtr> parts{};
  parts.push_back(NewText("when "));

  if (rhs.condition)
    parts.push_back(Lower(*rhs.condition));

  if (rhs.body) {
    if (IsBracedBlock(rhs.body)) {
      parts.push_back(NewText(" "));
      parts.push_back(Lower(*rhs.body));
    } else {
      parts.push_back(NewIndent(GetTabWidth(), NewConcat({
                                                   NewSoftLine(),
                                                   Lower(*rhs.body),
                                               })));
    }
  }

  return NewGroup(NewConcat(parts));
}

LOWER_FN(IfExpr) {
  std::vector<NodePtr> parts{};

  parts.push_back(NewText("if "));
  parts.push_back(Lower(*rhs.condition));

  if (rhs.then_branch) {
    if (IsBracedBlock(rhs.then_branch)) {
      parts.push_back(NewText(" "));
      parts.push_back(Lower(*rhs.then_branch));
    } else {
      parts.push_back(NewText(" "));
      parts.push_back(NewIndent(GetTabWidth(), NewConcat({
                                                   NewSoftLine(),
                                                   Lower(*rhs.then_branch),
                                               })));
    }
  }

  if (rhs.else_branch) {
    if (IsBracedBlock(rhs.then_branch)) {
      parts.push_back(NewText(" else"));
    } else {
      parts.push_back(NewSoftLine());
      parts.push_back(NewText("else"));
    }

    if (rhs.else_branch->Is<syntax::IfExpr>()) {
      parts.push_back(NewText(" "));
      parts.push_back(Lower(*rhs.else_branch));
    } else if (IsBracedBlock(rhs.else_branch)) {
      parts.push_back(NewText(" "));
      parts.push_back(Lower(*rhs.else_branch));
    } else {
      parts.push_back(NewText(" "));
      parts.push_back(NewIndent(GetTabWidth(), NewConcat({
                                                   NewSoftLine(),
                                                   Lower(*rhs.else_branch),
                                               })));
    }
  }

  return NewGroup(NewConcat(parts));
}

LOWER_FN(StringTemplate) {
  std::vector<NodePtr> parts{};

  for (auto i = 0; i < rhs.texts.size(); i++) {
    const auto& text = rhs.texts[i];
    parts.push_back(NewText(text));
    if (i < rhs.exprs.size()) {
      parts.push_back(NewConcat({
          NewText("{"),
          Lower(*rhs.exprs[i]),
          NewText("}"),
      }));
    }
  }

  return NewGroup(DoubleQuote(NewConcat(parts)));
}

LOWER_FN(BindingPattern) {
  return FormatName(rhs.name);
}

LOWER_FN(RecordUpdate) {
  std::vector<NodePtr> entries{};
  for (const auto& e : rhs.entries)
    entries.push_back(Lower(*e));

  return NewGroup(NewConcat({
      NewText("{"),
      NewBreakIf(NewEmpty(), NewText(" ")),
      NewIndent(GetTabWidth(), NewConcat({
                                   NewSoftLine(),
                                   Lower(*rhs.base),
                                   NewText(" |"),
                                   NewLine(),
                                   Punctuate(NewConcat({NewText(","), NewLine()}), entries),
                                   NewBreakIf(NewText(","), NewEmpty()),
                               })),
      NewSoftLine(),
      NewBreakIf(NewEmpty(), NewText(" ")),
      NewText("}"),
  }));
}

LOWER_FN(SetRule) {
  std::vector<NodePtr> props{};
  for (const auto& p : rhs.props)
    props.push_back(Lower(*p));

  return NewGroup(NewConcat({
      NewText("set "),
      FormatName(rhs.tag),
      NewGroup(NewConcat({
          NewText("("),
          NewIndent(GetTabWidth(), NewConcat({
                                       NewSoftLine(),
                                       Punctuate(NewConcat({NewText(","), NewLine()}), props),
                                       NewBreakIf(NewText(","), NewEmpty()),
                                   })),
          NewSoftLine(),
          NewText(")"),
      })),
  }));
}

LOWER_FN(UpdateEntry) {
  std::vector<NodePtr> parts{};

  for (size_t i = 0; i < rhs.path.size(); ++i) {
    parts.push_back(FormatName(rhs.path[i]));
    if (i + 1 < rhs.path.size())
      parts.push_back(NewText("."));
  }

  switch (rhs.op) {
    case syntax::UpdateEntry::Op::kAdd:
      parts.push_back(NewText(" +="));
      break;
    case syntax::UpdateEntry::Op::kDiv:
      parts.push_back(NewText(" /="));
      break;
    case syntax::UpdateEntry::Op::kMod:
      parts.push_back(NewText(" %="));
      break;
    case syntax::UpdateEntry::Op::kMul:
      parts.push_back(NewText(" *="));
      break;
    case syntax::UpdateEntry::Op::kSub:
      parts.push_back(NewText(" -="));
      break;
    case syntax::UpdateEntry::Op::kSet:
      parts.push_back(NewText(":"));
      break;
  }

  parts.push_back(NewIndent(GetTabWidth(), NewConcat({
                                               NewLine(),
                                               Lower(*rhs.value),
                                           })));

  return NewGroup(NewConcat(parts));
}

auto DocumentBuilder::Lower(const syntax::Node& node) -> NodePtr {
  switch (node.GetKind()) {
#define LOWER(Name)           \
  case syntax::Node::k##Name: \
    return Lower##Name(*node.As<syntax::Name>());

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
    LOWER(Block);
    LOWER(Pipe)
    LOWER(ForExpr);
    LOWER(Paren);
    LOWER(BoolLit);
    LOWER(Index);
    LOWER(RangeExpr);
    LOWER(RangePattern);
    LOWER(RecordPattern);
    LOWER(FieldPattern);
    LOWER(QualifiedType);
    LOWER(StaticAssert);
    LOWER(TestDecl);
    LOWER(ExternDecl);
    LOWER(IsExpr);
    LOWER(VariantPattern);
    LOWER(VariantRef);
    LOWER(Lambda);
    LOWER(UiElement);
    LOWER(WhenExpr);
    LOWER(IfExpr);
    LOWER(Placeholder);
    LOWER(BindingPattern);
    LOWER(StringTemplate);
    LOWER(RecordUpdate);
    LOWER(SetRule);
    LOWER(UpdateEntry);

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
