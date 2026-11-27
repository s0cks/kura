#include "lsp/outline.h"

#include <algorithm>
#include <set>

#include "lsp/describe.h"

namespace kura::lsp {
using syntax::Node;

namespace {
auto MakeSymbol(Document& doc, const std::string& name, const std::string& detail, const Symbol::Kind kind,
                const Span span, const Span name_span) -> Symbol {
  // A name that is missing has an empty span where it should have been; the
  // outline still shows the declaration, under the whole of its span.
  const auto selection = name_span.empty() && name.empty() ? span : name_span;
  return Symbol{
      .name = name.empty() ? "<unnamed>" : name,
      .detail = detail,
      .kind = kind,
      .range = doc.ToRange(span),
      .selectionRange = doc.ToRange(Span(std::max(selection.begin, span.begin), std::min(selection.end, span.end))),
  };
}

void AddTypeChildren(Document& doc, const Bindings& bindings, const syntax::TypeDecl& type, Symbol& symbol) {
  const auto* decl = bindings.FindDeclOf(&type);
  if (!decl)
    return;

  for (const auto* child : bindings.GetChildren(decl->id)) {
    if (child->kind == DeclKind::kVariant) {
      const auto* v = child->node->As<syntax::VariantType>();
      symbol.children.push_back(MakeSymbol(doc, child->name, v && v->payload ? "(" + TypeToString(v->payload) + ")" : "",
                                           Symbol::kEnumMemberSymbol, child->span, child->name_span));
    } else if (child->kind == DeclKind::kField) {
      // Fields of record types nested inside a field belong to that field.
      const auto* f = child->node->As<syntax::Field>();
      symbol.children.push_back(MakeSymbol(doc, child->name, f ? TypeToString(f->type) : "", Symbol::kFieldSymbol,
                                           child->span, child->name_span));
    }
  }
}
}  // namespace

auto GetOutline(Document& doc) -> std::vector<Symbol> {
  std::vector<Symbol> symbols{};
  const auto* bindings = doc.GetBindings();
  if (!bindings)
    return symbols;

  for (const auto* item : doc.GetRoot()->items) {
    switch (item->GetKind()) {
      case Node::kFnDecl: {
        const auto* fn = item->As<syntax::FnDecl>();
        auto sig = SignatureOf(*fn);
        symbols.push_back(MakeSymbol(doc, fn->name.text, sig.substr(3 + fn->name.text.size() + (fn->name.text.empty() ? 0 : 1)),
                                     Symbol::kFunctionSymbol, fn->span, fn->name.span));
        break;
      }
      case Node::kExternDecl: {
        const auto* ext = item->As<syntax::ExternDecl>();
        symbols.push_back(MakeSymbol(doc, ext->c_name.text, "extern \"" + ext->symbol + "\"", Symbol::kFunctionSymbol,
                                     ext->span, ext->c_name.span));
        break;
      }
      case Node::kTypeDecl: {
        const auto* type = item->As<syntax::TypeDecl>();
        const auto kind = type->type && type->type->Is<syntax::RecordType>() ? Symbol::kStructSymbol
                          : type->type && (type->type->Is<syntax::UnionType>() || type->type->Is<syntax::VariantType>())
                              ? Symbol::kEnumSymbol
                              : Symbol::kClassSymbol;
        auto symbol = MakeSymbol(doc, type->name.text, "", kind, type->span, type->name.span);
        AddTypeChildren(doc, *bindings, *type, symbol);
        symbols.push_back(std::move(symbol));
        break;
      }
      case Node::kBinding: {
        const auto* b = item->As<syntax::Binding>();
        if (!b->pattern) {
          const auto* decl = bindings->FindDeclOf(b);
          const auto* t = decl ? bindings->GetDeclType(decl->id) : nullptr;
          symbols.push_back(MakeSymbol(doc, b->name.text, t ? TypeToString(t) : "", Symbol::kVariableSymbol, b->span,
                                       b->name.span));
        }
        break;
      }
      case Node::kUseDecl: {
        const auto* use = item->As<syntax::UseDecl>();
        const auto name = use->has_alias ? use->alias.text : use->header;
        symbols.push_back(MakeSymbol(doc, name, "\"" + use->header + "\"", Symbol::kModuleSymbol, use->span,
                                     use->has_alias ? use->alias.span : use->header_span));
        break;
      }
      case Node::kTestDecl: {
        const auto* test = item->As<syntax::TestDecl>();
        symbols.push_back(MakeSymbol(doc, "test \"" + test->name + "\"", "", Symbol::kMethodSymbol, test->span,
                                     test->span));
        break;
      }
      default:
        break;
    }
  }
  return symbols;
}

namespace {
void CollectNodeFolds(Document& doc, const Node* node, std::set<std::pair<int, int>>& folds) {
  if (!node)
    return;

  bool foldable = false;
  switch (node->GetKind()) {
    case Node::kFnDecl:
    case Node::kExternDecl:
    case Node::kTypeDecl:
    case Node::kTestDecl:
    case Node::kBlock:
    case Node::kMatchExpr:
    case Node::kRecordLit:
    case Node::kRecordUpdate:
    case Node::kRecordType:
    case Node::kSeqLit:
    case Node::kUiElement:
    case Node::kIfExpr:
    case Node::kWhenExpr:
    case Node::kForExpr:
    case Node::kLambda:
    case Node::kCall:
      foldable = true;
      break;
    default:
      break;
  }

  if (foldable && node->span.end > node->span.begin) {
    const auto start = doc.ToPosition(node->span.begin).line;
    const auto end_pos = doc.ToPosition(node->span.end);
    auto end = end_pos.line;
    // Leave a closing bracket on a line of its own showing.
    const auto& text = doc.GetContents();
    if (end > start && node->span.end > 0 && node->span.end <= text.size()) {
      const auto last = text[node->span.end - 1];
      if (last == '}' || last == ')' || last == ']')
        end -= end_pos.character == 0 ? 0 : 1;
    }
    if (end > start)
      folds.insert({start, end});
  }

  node->ForEachChild([&](const Node* child) { CollectNodeFolds(doc, child, folds); });
}
}  // namespace

auto GetFoldingRanges(Document& doc) -> std::vector<FoldingRange> {
  std::vector<FoldingRange> ranges{};
  if (!doc.EnsureTree())
    return ranges;

  std::set<std::pair<int, int>> folds{};
  CollectNodeFolds(doc, doc.GetRoot(), folds);
  for (const auto& [start, end] : folds)
    ranges.push_back(FoldingRange{.start_line = start, .end_line = end});

  // Runs of comment lines and of `use` lines.
  const auto& source = doc.GetSource();
  const auto& text = source.GetText();
  const auto lines = static_cast<int>(source.GetNumberOfLines());
  const auto classify = [&](const int line) -> std::string {
    const auto span = source.GetLineSpan(static_cast<uint32_t>(line));
    auto view = std::string_view(text).substr(span.begin, span.end - span.begin);
    const auto first = view.find_first_not_of(" \t");
    if (first == std::string_view::npos)
      return "";
    view.remove_prefix(first);
    if (view.starts_with("//"))
      return "comment";
    if (view.starts_with("use ") || view.starts_with("use\t"))
      return "imports";
    return "";
  };

  for (int line = 0; line < lines;) {
    const auto kind = classify(line);
    if (kind.empty()) {
      line++;
      continue;
    }
    int end = line;
    while (end + 1 < lines && classify(end + 1) == kind)
      end++;
    if (end > line)
      ranges.push_back(FoldingRange{.start_line = line, .end_line = end, .kind = kind});
    line = end + 1;
  }

  std::sort(ranges.begin(), ranges.end(), [](const FoldingRange& a, const FoldingRange& b) {
    return std::tie(a.start_line, a.end_line) < std::tie(b.start_line, b.end_line);
  });
  return ranges;
}
}  // namespace kura::lsp
