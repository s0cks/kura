#include "lsp/hover.h"

#include <format>
#include <limits>

#include "dom/kind.h"
#include "dom/property.h"
#include "lsp/describe.h"

namespace kura::lsp {
namespace {
auto Join(const std::vector<std::string>& items, const std::string_view separator) -> std::string {
  std::string out{};
  for (size_t i = 0; i < items.size(); i++)
    out += (i ? std::string(separator) : std::string{}) + items[i];
  return out;
}

auto DescribeElement(const dom::Kind kind, const std::string& tag) -> std::string {
  std::vector<std::string> names{};
  for (const auto* def : dom::PropertiesOf(kind))
    names.push_back(std::format("`{}`", def->name));

  auto out = std::format("```kura\n#{}\n```\nAn element.", tag);
  out += dom::CanHaveChildren(kind) ? " It can have children." : " It has no children.";
  if (!names.empty())
    out += "\n\nProperties: " + Join(names, ", ");
  return out;
}

auto DescribeProperty(const dom::PropertyDef& def, const std::string& tag) -> std::string {
  std::string out = std::format("```kura\n{}: {}\n```\nA property of `#{}`.", def.name, dom::DescribeAccepted(def), tag);
  if (def.event)
    out += " It takes the message to send, not a value.";

  if (!def.words.empty()) {
    std::vector<std::string> words{};
    for (const auto word : def.words)
      words.push_back(std::format("`\"{}\"`", word));
    out += "\n\nOne of: " + Join(words, ", ");
  }

  constexpr auto kInf = std::numeric_limits<double>::infinity();
  if (def.min != -kInf || def.max != kInf) {
    if (def.min != -kInf && def.max != kInf)
      out += std::format("\n\nFrom {} to {}.", def.min, def.max);
    else if (def.min != -kInf)
      out += std::format("\n\nAt least {}.", def.min);
    else
      out += std::format("\n\nAt most {}.", def.max);
  }
  return out;
}

auto MakeHover(Document& doc, const Span span, std::string markdown) -> Hover {
  return Hover{
      .range = doc.ToRange(span),
      .contents = MarkupContent{.kind = MarkupContent::kMarkdown, .value = std::move(markdown)},
  };
}

// The element or `set` rule's tag and properties, which are not declared in
// the file but known to the compiler.
auto HoverElement(Document& doc, const syntax::Node* node, const uint32_t offset) -> std::optional<Hover> {
  const syntax::Name* tag = nullptr;
  const std::vector<syntax::Node*>* props = nullptr;
  Span head{};

  if (const auto* element = node->As<syntax::UiElement>()) {
    tag = &element->tag;
    props = &element->props;
    head = Span(element->span.begin, element->tag.span.end);
  } else if (const auto* set = node->As<syntax::SetRule>()) {
    tag = &set->tag;
    props = &set->props;
    head = Span(set->span.begin, set->tag.span.end);
  } else {
    return std::nullopt;
  }

  const auto kind = dom::FindKind(tag->text);
  if (!kind)
    return std::nullopt;

  if (head.Touches(offset))
    return MakeHover(doc, tag->span, DescribeElement(*kind, tag->text));

  for (const auto* p : *props) {
    const auto* arg = p->As<syntax::Arg>();
    if (!arg || arg->name.missing || !arg->name.span.Touches(offset))
      continue;
    if (const auto* def = dom::FindProperty(*kind, arg->name.text))
      return MakeHover(doc, arg->name.span, DescribeProperty(*def, tag->text));
  }
  return std::nullopt;
}
}  // namespace

auto GetHover(Document& doc, const Position& pos, ModuleResolver* modules) -> std::optional<Hover> {
  const auto* bindings = doc.GetBindings();
  if (!bindings)
    return std::nullopt;

  const auto offset = doc.ToOffset(pos);
  if (const auto* occurrence = bindings->FindOccurrenceAt(offset)) {
    return MakeHover(doc, occurrence->span,
                     DescribeDecl(*bindings, bindings->GetDecl(occurrence->decl), doc.GetContents()));
  }

  if (modules) {
    if (const auto* qualified = bindings->FindQualifiedRefAt(offset)) {
      if (const auto target = ResolveQualified(*modules, doc, *qualified)) {
        return MakeHover(doc, qualified->span,
                         DescribeDecl(*target->doc->GetBindings(), *target->decl, target->doc->GetContents()));
      }
    }
  }

  const auto path = syntax::FindPathAt(doc.GetTree()->GetRoot(), offset);
  if (path.empty())
    return std::nullopt;

  // A built-in type is not declared anywhere.
  if (const auto* named = path.back()->As<syntax::NamedType>(); named && named->name.span.Touches(offset)) {
    const auto text = DescribeBuiltinType(named->name.text);
    if (!text.empty()) {
      return MakeHover(doc, named->name.span,
                       std::format("```kura\n{}\n```\n{}", named->name.text, std::string(text)));
    }
  }

  // The closest element around the cursor decides what its properties are.
  for (auto it = path.rbegin(); it != path.rend(); ++it) {
    if ((*it)->Is<syntax::UiElement>() || (*it)->Is<syntax::SetRule>()) {
      if (auto hover = HoverElement(doc, *it, offset))
        return hover;
      break;
    }
  }
  return std::nullopt;
}
}  // namespace kura::lsp
