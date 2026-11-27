#include "lsp/completion.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <set>

#include "dom/kind.h"
#include "dom/property.h"
#include "frontend/syntax/token.h"
#include "lsp/describe.h"
#include "lsp/keyword_completion.h"

namespace kura::lsp {
namespace {
auto IsWordChar(const char c) -> bool {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
}

struct TextState {
  bool in_comment = false;
  bool in_string = false;
  // Inside a `{...}` of an interpolated string; the position is code again.
  bool in_interpolation = false;
  uint32_t string_begin = 0;
};

struct Opener {
  char open = 0;
  uint32_t at = 0;
};

// One pass over the text up to the offset: what the offset is inside of, and
// which brackets are open there.
auto Scan(const std::string_view text, uint32_t offset, std::vector<Opener>& openers) -> TextState {
  offset = std::min<uint32_t>(offset, static_cast<uint32_t>(text.size()));
  TextState state{};
  std::vector<Opener> interpolation_base{};

  for (uint32_t idx = 0; idx < offset; idx++) {
    const auto c = text[idx];
    if (state.in_string) {
      if (c == '\\') {
        idx++;
      } else if (c == '"') {
        state.in_string = false;
        state.in_interpolation = false;
      } else if (c == '{') {
        // `{name}` in a string is code until the matching `}`.
        openers.push_back({'{', idx});
        state.in_interpolation = true;
        state.in_string = false;
        interpolation_base.push_back({'"', idx});
      }
      continue;
    }

    if (c == '/' && idx + 1 < offset && text[idx + 1] == '/') {
      while (idx < offset && text[idx] != '\n')
        idx++;
      if (idx >= offset) {
        state.in_comment = true;
        return state;
      }
      continue;
    }

    if (c == '"') {
      state.in_string = true;
      state.string_begin = idx;
      continue;
    }

    if (c == '(' || c == '[' || c == '{') {
      openers.push_back({c, idx});
    } else if (c == ')' || c == ']' || c == '}') {
      if (!openers.empty())
        openers.pop_back();
      if (c == '}' && state.in_interpolation && !interpolation_base.empty() && openers.size() < interpolation_base.size()) {
        interpolation_base.pop_back();
        state.in_interpolation = false;
        // The string goes on after the `}`.
        state.in_string = true;
      }
    }
  }

  if (state.in_interpolation) {
    state.in_string = false;
  } else if (!state.in_string) {
    state.in_interpolation = false;
  }
  return state;
}

auto SkipSpaceBack(const std::string_view text, uint32_t at) -> uint32_t {
  while (at > 0 && std::isspace(static_cast<unsigned char>(text[at - 1])))
    at--;
  return at;
}

auto WordBefore(const std::string_view text, uint32_t end) -> std::string {
  auto begin = end;
  while (begin > 0 && IsWordChar(text[begin - 1]))
    begin--;
  return std::string(text.substr(begin, end - begin));
}

// `a.b.c` ending at `end`.
auto ChainBefore(const std::string_view text, uint32_t end) -> std::vector<std::string> {
  std::vector<std::string> chain{};
  while (true) {
    auto word_end = end;
    auto begin = end;
    while (begin > 0 && IsWordChar(text[begin - 1]))
      begin--;
    if (begin == word_end)
      break;
    chain.insert(chain.begin(), std::string(text.substr(begin, word_end - begin)));
    if (begin > 0 && text[begin - 1] == '.' && !(begin > 1 && text[begin - 2] == '.')) {
      end = begin - 1;
      continue;
    }
    break;
  }
  return chain;
}

// The property names written between `open` and `limit`, to not offer twice.
auto PropertiesWritten(const std::string_view text, const uint32_t open, const uint32_t limit) -> std::vector<std::string> {
  std::vector<std::string> names{};
  int depth = 0;
  for (auto idx = open + 1; idx < limit; idx++) {
    const auto c = text[idx];
    if (c == '(' || c == '[' || c == '{') {
      depth++;
    } else if (c == ')' || c == ']' || c == '}') {
      depth--;
    } else if (c == '"') {
      for (idx++; idx < limit && text[idx] != '"'; idx++) {
        if (text[idx] == '\\')
          idx++;
      }
    } else if (c == ':' && depth == 0) {
      auto name = WordBefore(text, idx);
      if (!name.empty())
        names.push_back(std::move(name));
    }
  }
  return names;
}

auto StartsWithWord(const std::string_view text, const uint32_t at, const std::string_view word) -> bool {
  return text.substr(at).starts_with(word) &&
         (at + word.size() >= text.size() || !IsWordChar(text[at + word.size()]));
}

// The tag of `#tag(` or `set tag(` that `open` belongs to; empty for any other paren.
auto ElementTagOf(const std::string_view text, const uint32_t open) -> std::string {
  const auto end = SkipSpaceBack(text, open);
  const auto tag = WordBefore(text, end);
  if (tag.empty())
    return {};
  const auto before = end - static_cast<uint32_t>(tag.size());
  if (before > 0 && text[before - 1] == '#')
    return tag;

  const auto word_start = SkipSpaceBack(text, before);
  if (word_start >= 3 && text.substr(word_start - 3, 3) == "set" && (word_start == 3 || !IsWordChar(text[word_start - 4])))
    return tag;
  return {};
}

// `fn name(`, `extern "x" fn name(`.
auto IsParameterList(const std::string_view text, const uint32_t open) -> bool {
  const auto end = SkipSpaceBack(text, open);
  const auto name = WordBefore(text, end);
  if (name.empty())
    return false;
  const auto before = SkipSpaceBack(text, end - static_cast<uint32_t>(name.size()));
  return before >= 2 && text.substr(before - 2, 2) == "fn" && (before == 2 || !IsWordChar(text[before - 3]));
}

// `type Name = {` or `type Name<T> = {`, where `open` is the brace.
auto IsRecordTypeBody(const std::string_view text, const uint32_t open) -> bool {
  auto at = SkipSpaceBack(text, open);
  if (at == 0 || text[at - 1] != '=')
    return false;
  const auto line_begin = text.rfind('\n', at == 0 ? 0 : at - 1);
  const auto line = text.substr(line_begin == std::string_view::npos ? 0 : line_begin + 1);
  const auto first = line.find_first_not_of(" \t");
  return first != std::string_view::npos && line.substr(first).starts_with("type");
}
}  // namespace

auto GetCompletionContext(const std::string_view text, uint32_t offset) -> CompletionContext {
  offset = std::min<uint32_t>(offset, static_cast<uint32_t>(text.size()));
  CompletionContext ctx{};

  auto word_begin = offset;
  while (word_begin > 0 && IsWordChar(text[word_begin - 1]))
    word_begin--;
  ctx.word_begin = word_begin;
  ctx.word_end = offset;

  std::vector<Opener> openers{};
  const auto state = Scan(text, word_begin, openers);
  if (state.in_comment)
    return ctx;

  if (state.in_string && !state.in_interpolation) {
    // Only `use "` completes inside a string: a header path.
    const auto line_begin = text.rfind('\n', state.string_begin == 0 ? 0 : state.string_begin - 1);
    const auto line_from = line_begin == std::string_view::npos ? 0 : line_begin + 1;
    const auto before = text.substr(line_from, state.string_begin - line_from);
    const auto first = before.find_first_not_of(" \t");
    if (first != std::string_view::npos && before.substr(first).starts_with("use")) {
      ctx.kind = CompletionContext::kHeaderPath;
      ctx.prefix = std::string(text.substr(state.string_begin + 1, offset - state.string_begin - 1));
      ctx.word_begin = state.string_begin + 1;
    }
    return ctx;
  }

  const auto prev_at = SkipSpaceBack(text, word_begin);
  const char prev = prev_at > 0 ? text[prev_at - 1] : '\n';
  const bool adjacent = prev_at == word_begin;

  if (adjacent && prev == '#') {
    ctx.kind = CompletionContext::kElementTag;
    return ctx;
  }
  if (adjacent && prev == '@') {
    ctx.kind = CompletionContext::kDirective;
    return ctx;
  }
  if (adjacent && prev == '.' && !(prev_at > 1 && text[prev_at - 2] == '.')) {
    ctx.kind = CompletionContext::kMember;
    ctx.chain = ChainBefore(text, prev_at - 1);
    return ctx;
  }

  const Opener* inner = openers.empty() ? nullptr : &openers.back();
  if (!inner) {
    ctx.kind = CompletionContext::kTopLevel;
    // `let x: |` and `fn f(): |` at depth 0 want a type.
    if (prev == ':') {
      ctx.kind = CompletionContext::kType;
    }
    return ctx;
  }

  if (inner->open == '(') {
    if (const auto tag = ElementTagOf(text, inner->at); !tag.empty()) {
      ctx.tag = tag;
      if (prev == ':') {
        ctx.kind = CompletionContext::kPropertyValue;
        ctx.property = WordBefore(text, prev_at - 1);
      } else if (prev == '(' || prev == ',') {
        ctx.kind = CompletionContext::kPropertyName;
        ctx.present = PropertiesWritten(text, inner->at, word_begin);
      } else {
        ctx.kind = CompletionContext::kExpression;
      }
      return ctx;
    }

    if (IsParameterList(text, inner->at)) {
      ctx.kind = prev == ':' ? CompletionContext::kType : CompletionContext::kNone;
      return ctx;
    }
  }

  if (inner->open == '{' && IsRecordTypeBody(text, inner->at)) {
    ctx.kind = prev == ':' ? CompletionContext::kType : CompletionContext::kNone;
    return ctx;
  }

  // `fn f(a: Int): |` and `let x: |` in a body.
  if (prev == ':' && prev_at >= 2) {
    const auto before = SkipSpaceBack(text, prev_at - 1);
    if (before > 0 && text[before - 1] == ')') {
      ctx.kind = CompletionContext::kType;
      return ctx;
    }
    const auto line_begin = text.rfind('\n', prev_at - 1);
    const auto line = text.substr(line_begin == std::string_view::npos ? 0 : line_begin + 1);
    const auto first = line.find_first_not_of(" \t");
    if (first != std::string_view::npos && line.substr(first).starts_with("let")) {
      ctx.kind = CompletionContext::kType;
      return ctx;
    }
  }

  ctx.kind = CompletionContext::kExpression;
  return ctx;
}

auto FindEnclosingCall(const std::string_view text, uint32_t offset) -> std::optional<CallSite> {
  offset = std::min<uint32_t>(offset, static_cast<uint32_t>(text.size()));
  std::vector<Opener> openers{};
  const auto state = Scan(text, offset, openers);
  if (state.in_comment || (state.in_string && !state.in_interpolation))
    return std::nullopt;

  // The closest `(`; brackets and braces inside an argument don't end the call.
  const Opener* paren = nullptr;
  for (auto it = openers.rbegin(); it != openers.rend(); ++it) {
    if (it->open == '(') {
      paren = &*it;
      break;
    }
  }
  if (!paren)
    return std::nullopt;

  CallSite site{};
  site.open = paren->at;
  if (const auto tag = ElementTagOf(text, paren->at); !tag.empty()) {
    site.is_element = true;
    site.tag = tag;
  } else {
    site.callee = ChainBefore(text, SkipSpaceBack(text, paren->at));
    if (site.callee.empty())
      return std::nullopt;
  }

  // Commas at the top level of the parentheses separate the arguments.
  int depth = 0;
  uint32_t segment = paren->at + 1;
  for (auto idx = paren->at + 1; idx < offset; idx++) {
    const auto c = text[idx];
    if (c == '"') {
      for (idx++; idx < offset && text[idx] != '"'; idx++) {
        if (text[idx] == '\\')
          idx++;
      }
    } else if (c == '(' || c == '[' || c == '{') {
      depth++;
    } else if (c == ')' || c == ']' || c == '}') {
      depth--;
    } else if (c == ',' && depth == 0) {
      site.active_index++;
      segment = idx + 1;
    }
  }

  if (site.is_element) {
    for (auto idx = segment; idx < offset; idx++) {
      if (text[idx] == ':') {
        site.active_name = WordBefore(text, idx);
        break;
      }
    }
  }
  return site;
}

namespace {
struct Builder {
  Document& doc;
  const CompletionContext& ctx;
  std::vector<CompletionItem> items{};
  std::set<std::string> seen{};

  auto Range() const -> lsp::Range {
    return lsp::Range{doc.ToPosition(ctx.word_begin), doc.ToPosition(ctx.word_end)};
  }

  void Add(const CompletionItem::Kind kind, const std::string& label, const std::string& detail, std::string insert = {},
           const bool snippet = false, const std::string& sort = {}, const bool replace = true) {
    if (!seen.insert(label + "\x1f" + std::to_string(kind)).second)
      return;

    if (insert.empty())
      insert = label;
    CompletionItem item{
        .kind = kind,
        .label = label,
        .detail = detail,
        .sort = sort.empty() ? "5" + label : sort + label,
        .insertText = insert,
        .insertTextFormat = snippet ? CompletionItem::kSnippetFormat : CompletionItem::kPlainTextFormat,
    };
    if (replace)
      item.textEdit = TextEdit{.range = Range(), .newText = insert};
    items.push_back(std::move(item));
  }
};

auto BuiltinFields(const std::string_view type) -> std::vector<std::string> {
  if (type == "Size")
    return {"width", "height"};
  if (type == "Vec2")
    return {"x", "y"};
  if (type == "Vec3")
    return {"x", "y", "z"};
  if (type == "Rect")
    return {"x", "y", "width", "height"};
  if (type == "Color" || type == "Rgba")
    return {"r", "g", "b", "a"};
  if (type == "Hsla")
    return {"h", "s", "l", "a"};
  if (type == "Hsba" || type == "Hsva")
    return {"h", "s", "b", "a"};
  return {};
}

// The fields written directly in a record type, found through a name if it is one.
auto RecordFieldsOf(const Bindings& bindings, const syntax::Node* type) -> std::vector<const syntax::Field*> {
  std::vector<const syntax::Field*> fields{};
  if (!type)
    return fields;

  if (const auto* named = type->As<syntax::NamedType>()) {
    for (const auto& decl : bindings.GetDecls()) {
      if (decl.kind == DeclKind::kType && decl.name == named->name.GetValue())
        return RecordFieldsOf(bindings, decl.node->As<syntax::TypeDecl>()->type);
    }
    return fields;
  }

  if (const auto* record = type->As<syntax::RecordType>()) {
    for (const auto* f : record->fields) {
      if (const auto* field = f->As<syntax::Field>())
        fields.push_back(field);
    }
  }
  return fields;
}

void CompleteModuleMember(Builder& b, Document& from, const Decl& alias, ModuleResolver* modules) {
  // The names in a C header are not known here, but the ones in a `.kui` file are.
  const auto* use = alias.node ? alias.node->As<syntax::UseDecl>() : nullptr;
  auto* module = use && modules ? modules->LoadUse(from, *use) : nullptr;
  if (!module)
    return;

  for (const auto ns : {Ns::kValue, Ns::kType}) {
    for (const auto* decl : GetTopLevelDecls(*module, ns)) {
      switch (decl->kind) {
        case DeclKind::kFn:
          b.Add(CompletionItem::kFunction, decl->name, SignatureOf(*decl->node->As<syntax::FnDecl>()), decl->name + "($1)", true, "1");
          break;
        case DeclKind::kType:
          b.Add(CompletionItem::kStruct, decl->name, "type", {}, false, "1");
          break;
        case DeclKind::kVariant:
          b.Add(CompletionItem::kEnumMember, decl->name, "variant", {}, false, "2");
          break;
        default:
          b.Add(CompletionItem::kVariable, decl->name, std::string(ToString(decl->kind)), {}, false, "1");
          break;
      }
    }
  }
}

void CompleteMember(Builder& b, Document& doc, const Bindings& bindings, const uint32_t offset, ModuleResolver* modules) {
  const auto& chain = b.ctx.chain;
  if (chain.empty())
    return;

  const auto* first = bindings.Lookup(chain.front(), Ns::kValue, offset);
  if (first && first->kind == DeclKind::kAlias) {
    if (chain.size() == 1)
      CompleteModuleMember(b, doc, *first, modules);
    return;
  }

  const syntax::Node* type = first ? bindings.GetDeclType(first->id) : nullptr;
  bool known = type != nullptr;
  for (size_t i = 1; i < chain.size() && known; i++) {
    const auto fields = RecordFieldsOf(bindings, type);
    const auto it = std::find_if(fields.begin(), fields.end(), [&](const syntax::Field* f) { return f->name.GetValue() == chain[i]; });
    if (it == fields.end()) {
      known = false;
      break;
    }
    type = (*it)->type;
  }

  if (known) {
    for (const auto* field : RecordFieldsOf(bindings, type))
      b.Add(CompletionItem::kField, field->name.GetValue(), TypeToString(field->type), {}, false, "1");
    if (const auto* named = type->As<syntax::NamedType>()) {
      for (const auto& name : BuiltinFields(named->name.text))
        b.Add(CompletionItem::kField, name, named->name.text, {}, false, "1");
    }
    return;
  }

  // The type of what is in front of the `.` isn't known: every field in the file may be it.
  for (const auto& decl : bindings.GetDecls()) {
    if (decl.kind != DeclKind::kField)
      continue;
    const auto* field = decl.node->As<syntax::Field>();
    b.Add(CompletionItem::kField, decl.name, field ? TypeToString(field->type) : "", {}, false, "8");
  }
}

void CompleteTypes(Builder& b, const Bindings& bindings, const uint32_t offset) {
  for (const auto* decl : bindings.GetVisibleAt(offset, Ns::kType)) {
    if (decl->kind == DeclKind::kType)
      b.Add(CompletionItem::kStruct, decl->name, "type", {}, false, "1");
    else if (decl->kind == DeclKind::kTypeParam)
      b.Add(CompletionItem::kTypeParameter, decl->name, "type parameter", {}, false, "0");
  }
  for (const auto name : GetBuiltinTypes()) {
    const auto text = std::string(name);
    const bool generic = name == "Seq" || name == "Optional";
    b.Add(CompletionItem::kClass, text, std::string(DescribeBuiltinType(name)), generic ? text + "<${1}>" : text, generic,
          "2");
  }
  for (const auto& decl : bindings.GetDecls()) {
    if (decl.kind == DeclKind::kAlias && !decl.implicit)
      b.Add(CompletionItem::kModule, decl.name, "header", {}, false, "3");
  }
}

void AddKeyword(Builder& b, const std::string& label, const std::string& detail, const std::string& snippet) {
  b.Add(CompletionItem::kKeyword, label, detail, snippet, snippet.find('$') != std::string::npos, "7");
}

void CompleteExpression(Builder& b, const Bindings& bindings, const uint32_t offset) {
  const auto& text = b.doc.GetContents();
  for (const auto* decl : bindings.GetVisibleAt(offset, Ns::kValue)) {
    // `let a = §`: the binding's span ends where the half-written value does, so
    // by the tree `a` already exists; by the line it is still being defined.
    if (decl->kind == DeclKind::kLet && offset >= decl->name_span.end &&
        text.find('\n', decl->name_span.end) >= std::min<size_t>(offset, text.size()))
      continue;

    switch (decl->kind) {
      case DeclKind::kFn: {
        const auto* fn = decl->node->As<syntax::FnDecl>();
        const bool no_params = fn && fn->params.empty();
        b.Add(CompletionItem::kFunction, decl->name, fn ? SignatureOf(*fn) : "function", no_params ? decl->name + "()" : decl->name + "($1)", true, "3");
        break;
      }
      case DeclKind::kExtern: {
        const auto* ext = decl->node->As<syntax::ExternDecl>();
        b.Add(CompletionItem::kFunction, decl->name, ext ? SignatureOf(*ext) : "extern function", decl->name + "($1)", true, "3");
        break;
      }
      case DeclKind::kAlias:
        b.Add(CompletionItem::kModule, decl->name, "header", {}, false, "4");
        break;
      default: {
        const auto* type = bindings.GetDeclType(decl->id);
        b.Add(CompletionItem::kVariable, decl->name, type ? TypeToString(type) : std::string(ToString(decl->kind)), {}, false, "1");
        break;
      }
    }
  }

  for (const auto* decl : bindings.GetVisibleAt(offset, Ns::kType)) {
    if (decl->kind != DeclKind::kVariant)
      continue;
    const auto* v = decl->node->As<syntax::VariantType>();
    const bool payload = v && v->payload;
    b.Add(CompletionItem::kEnumMember, decl->name, v ? TypeToString(v) : "variant", payload ? decl->name + "(${1})" : decl->name,
          payload, "2");
  }

  AddKeyword(b, "if", "conditional", "if ${1:condition} {\n\t$2\n} else {\n\t$0\n}");
  AddKeyword(b, "match", "pattern match", "match ${1:value} {\n\t${2:pattern} => ${3:result},\n\t_ => ${0:other}\n}");
  AddKeyword(b, "for", "loop", "for ${1:_}, ${2:item} in ${3:items} => ${0:item}");
  AddKeyword(b, "when", "conditional element", "when ${1:condition} {\n\t$0\n}");
  AddKeyword(b, "let", "binding", "let ${1:name} = $0");
  AddKeyword(b, "set", "style rule", "set ${1:text}(${0})");
  AddKeyword(b, "None", "no value", "None");
  AddKeyword(b, "true", "Bool", "true");
  AddKeyword(b, "false", "Bool", "false");
}

void CompleteElementTags(Builder& b) {
  const auto add = [&](const dom::Kind kind, const std::string_view tag) {
    const bool children = dom::CanHaveChildren(kind);
    const std::string name(tag);
    b.Add(CompletionItem::kClass, name, children ? "element, can have children" : "element", name, false, "1");
  };
#define ADD_ELEMENT(Name, Tag) add(dom::Kind::k##Name, Tag);
  FOR_EACH_ELEMENT_NODE(ADD_ELEMENT)
#undef ADD_ELEMENT
}

void CompleteDirectives(Builder& b) {
  b.Add(CompletionItem::kProperty, "meta", "attach metadata", "meta(tags: [ ${1} ])", true, "1");
  b.Add(CompletionItem::kProperty, "vec", "keep a Seq contiguous", {}, false, "1");
  b.Add(CompletionItem::kProperty, "list", "keep a Seq as a list", {}, false, "1");
  b.Add(CompletionItem::kProperty, "pure", "no side effects", {}, false, "1");
}

void CompletePropertyNames(Builder& b) {
  const auto kind = dom::FindKind(b.ctx.tag);
  if (!kind)
    return;

  for (const auto* def : dom::PropertiesOf(*kind)) {
    const std::string name(def->name);
    if (std::find(b.ctx.present.begin(), b.ctx.present.end(), name) != b.ctx.present.end())
      continue;
    b.Add(CompletionItem::kProperty, name, dom::DescribeAccepted(*def), name + ": ", false, "1");
  }
}

void CompletePropertyValues(Builder& b, const Bindings& bindings, const uint32_t offset) {
  const auto kind = dom::FindKind(b.ctx.tag);
  const auto* def = kind ? dom::FindProperty(*kind, b.ctx.property) : nullptr;
  if (def) {
    for (const auto word : def->words)
      b.Add(CompletionItem::kEnumMember, std::format("\"{}\"", word), "", {}, false, "0", true);
    if (def->Accepts(CompileType::kBool)) {
      b.Add(CompletionItem::kValue, "true", "Bool", {}, false, "0");
      b.Add(CompletionItem::kValue, "false", "Bool", {}, false, "0");
    }
    if (def->Accepts(CompileType::kPx))
      b.Add(CompletionItem::kUnit, "px", "pixels", "${1:16}px", true, "0");
    if (def->Accepts(CompileType::kPct))
      b.Add(CompletionItem::kUnit, "%", "percent of the parent", "${1:50}%", true, "0");
    if (def->Accepts(CompileType::kFr))
      b.Add(CompletionItem::kUnit, "fr", "share of the free space", "${1:1}fr", true, "0");
    if (def->event) {
      // An event property takes the message to send.
      for (const auto* decl : bindings.GetVisibleAt(offset, Ns::kType)) {
        if (decl->kind != DeclKind::kVariant)
          continue;
        const auto* v = decl->node->As<syntax::VariantType>();
        const bool payload = v && v->payload;
        b.Add(CompletionItem::kEnumMember, decl->name, v ? TypeToString(v) : "variant", payload ? decl->name + "(${1})" : decl->name,
              payload, "0");
      }
    }
    constexpr dom::TypeMask kColors =
#define COLOR_BIT(Name) dom::TypeBit(CompileType::k##Name) |
        FOR_EACH_COLOR_MODEL(COLOR_BIT)
#undef COLOR_BIT
        0;
    if ((def->accepts & kColors) != 0)
      b.Add(CompletionItem::kColor, "rgb", "color", "rgb(${1:255}, ${2:255}, ${3:255})", true, "1");
  }
  CompleteExpression(b, bindings, offset);
}

void CompleteHeaderPath(Builder& b, const Document& doc) {
  namespace fs = std::filesystem;
  // `use "std/..."` is read from the standard library, anything else is relative to the file.
  std::error_code ec{};
  fs::path base{};
  const auto uri = doc.GetUri();
  if (uri.starts_with("file://"))
    base = fs::path(uri.substr(7)).parent_path();
  if (base.empty())
    return;

  auto prefix = fs::path(b.ctx.prefix);
  const auto dir = b.ctx.prefix.empty() || b.ctx.prefix.back() == '/' ? base / prefix : base / prefix.parent_path();
  if (!fs::is_directory(dir, ec))
    return;

  for (const auto& entry : fs::directory_iterator(dir, ec)) {
    const auto name = entry.path().filename().string();
    if (name.starts_with("."))
      continue;
    if (entry.is_directory(ec)) {
      b.Add(CompletionItem::kFolder, name + "/", "directory", name + "/", false, "0");
    } else if (const auto ext = entry.path().extension().string(); ext == ".h" || ext == ".kui") {
      b.Add(CompletionItem::kFile, name, "header", name, false, "1");
    }
  }
}
}  // namespace

auto GetCompletions(Document& doc, const uint32_t offset, ModuleResolver* modules) -> std::vector<CompletionItem> {
  const auto ctx = GetCompletionContext(doc.GetContents(), offset);
  if (ctx.kind == CompletionContext::kNone)
    return {};

  Builder b{doc, ctx};
  const auto* bindings = doc.GetBindings();
  switch (ctx.kind) {
    case CompletionContext::kTopLevel: {
      auto items = GetDeclarationCompletions(doc, offset);
      b.items = std::move(items);
      for (const auto& item : b.items)
        b.seen.insert(item.label + "\x1f" + std::to_string(item.kind));
      const auto word = GetWordAtCursor(doc.GetContents(), offset);
      if (word.at_line_start) {
        const std::pair<const char*, const char*> keywords[] = {
            {"use", "use \"${1:header.h}\"${0}"},
            {"extern", "extern \"${1:symbol}\" fn ${2:name}(${3}): ${0:Type}"},
            {"test", "test \"${1:name}\" {\n\t$0\n}"},
            {"let", "let ${1:name} = $0"},
        };
        for (const auto& [label, body] : keywords) {
          if (std::string_view(label).starts_with(word.text))
            b.Add(CompletionItem::kSnippet, label, "declaration", body, true, "4");
        }
      }
      break;
    }
    case CompletionContext::kElementTag:
      CompleteElementTags(b);
      break;
    case CompletionContext::kDirective:
      CompleteDirectives(b);
      break;
    case CompletionContext::kPropertyName:
      CompletePropertyNames(b);
      break;
    case CompletionContext::kHeaderPath:
      CompleteHeaderPath(b, doc);
      break;
    case CompletionContext::kMember:
      if (bindings)
        CompleteMember(b, doc, *bindings, offset, modules);
      break;
    case CompletionContext::kType:
      if (bindings)
        CompleteTypes(b, *bindings, offset);
      break;
    case CompletionContext::kPropertyValue:
      if (bindings)
        CompletePropertyValues(b, *bindings, offset);
      break;
    case CompletionContext::kExpression:
      if (bindings)
        CompleteExpression(b, *bindings, offset);
      break;
    case CompletionContext::kNone:
      break;
  }
  return std::move(b.items);
}
}  // namespace kura::lsp
