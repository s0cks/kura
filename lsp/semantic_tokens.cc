#include "lsp/semantic_tokens.h"

#include <algorithm>

#include "dom/kind.h"
#include "dom/property.h"

namespace kura::lsp {
namespace {
enum TokenType : uint32_t {
  kNamespace,
  kType,
  kTypeParameter,
  kEnumMember,
  kFunction,
  kParameter,
  kVariable,
  kProperty,
  kClass,
  kMacro,
};

enum TokenModifier : uint32_t {
  kDeclaration = 1u << 0,
  kReadonly = 1u << 1,
};

struct Token {
  uint32_t begin = 0;
  uint32_t end = 0;
  uint32_t type = 0;
  uint32_t modifiers = 0;
};

auto TypeOf(const DeclKind kind) -> TokenType {
  switch (kind) {
    case DeclKind::kFn:
    case DeclKind::kExtern:
      return kFunction;
    case DeclKind::kType:
      return kType;
    case DeclKind::kTypeParam:
      return kTypeParameter;
    case DeclKind::kVariant:
      return kEnumMember;
    case DeclKind::kField:
      return kProperty;
    case DeclKind::kParam:
    case DeclKind::kLambdaParam:
      return kParameter;
    case DeclKind::kAlias:
      return kNamespace;
    default:
      return kVariable;
  }
}

void CollectElements(const syntax::Node* node, std::vector<Token>& tokens) {
  if (!node)
    return;

  const syntax::Name* tag = nullptr;
  const std::vector<syntax::Node*>* props = nullptr;
  if (const auto* element = node->As<syntax::UiElement>()) {
    tag = &element->tag;
    props = &element->props;
  } else if (const auto* set = node->As<syntax::SetRule>()) {
    tag = &set->tag;
    props = &set->props;
  }

  if (tag && !tag->missing) {
    if (const auto kind = dom::FindKind(tag->text)) {
      tokens.push_back(Token{tag->span.begin, tag->span.end, kClass, 0});
      for (const auto* p : *props) {
        const auto* arg = p->As<syntax::Arg>();
        if (arg && !arg->name.missing && dom::FindProperty(*kind, arg->name.text))
          tokens.push_back(Token{arg->name.span.begin, arg->name.span.end, kProperty, 0});
      }
    }
  }

  if (const auto* directive = node->As<syntax::Directive>(); directive && !directive->name.missing)
    tokens.push_back(Token{directive->name.span.begin, directive->name.span.end, kMacro, 0});

  node->ForEachChild([&](const syntax::Node* child) { CollectElements(child, tokens); });
}
}  // namespace

auto GetSemanticTokensLegend() -> SemanticTokensLegend {
  return SemanticTokensLegend{
      .tokenTypes = {"namespace", "type", "typeParameter", "enumMember", "function", "parameter", "variable", "property",
                     "class", "macro"},
      .tokenModifiers = {"declaration", "readonly"},
  };
}

auto GetSemanticTokens(Document& doc) -> std::vector<uint32_t> {
  std::vector<uint32_t> data{};
  const auto* bindings = doc.GetBindings();
  if (!bindings)
    return data;

  std::vector<Token> tokens{};
  for (const auto& o : bindings->GetOccurrences()) {
    // A guess is not told apart from other text: colouring it would claim more than is known.
    if (o.is_guess)
      continue;
    const auto& decl = bindings->GetDecl(o.decl);
    // `{ x }` is a field and a variable at once; the binding is what is new.
    uint32_t modifiers = 0;
    if (o.is_declaration)
      modifiers |= kDeclaration;
    if (decl.kind == DeclKind::kLet && !decl.name.empty())
      modifiers |= kReadonly;
    tokens.push_back(Token{o.span.begin, o.span.end, TypeOf(decl.kind), modifiers});
  }
  CollectElements(doc.GetRoot(), tokens);

  std::stable_sort(tokens.begin(), tokens.end(), [](const Token& a, const Token& b) { return a.begin < b.begin; });
  // A pun puts two tokens on one name; keep the first.
  tokens.erase(std::unique(tokens.begin(), tokens.end(), [](const Token& a, const Token& b) { return a.begin == b.begin; }),
               tokens.end());

  int prev_line = 0;
  int prev_char = 0;
  for (const auto& token : tokens) {
    const auto start = doc.ToPosition(token.begin);
    const auto end = doc.ToPosition(token.end);
    // Tokens can't span lines.
    if (start.line != end.line || end.character <= start.character)
      continue;

    const auto delta_line = start.line - prev_line;
    const auto delta_char = delta_line == 0 ? start.character - prev_char : start.character;
    data.insert(data.end(), {static_cast<uint32_t>(delta_line), static_cast<uint32_t>(delta_char),
                             static_cast<uint32_t>(end.character - start.character), token.type, token.modifiers});
    prev_line = start.line;
    prev_char = start.character;
  }
  return data;
}
}  // namespace kura::lsp
