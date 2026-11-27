#include "lsp/navigation.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <set>
#include <tuple>

#include "frontend/syntax/token.h"

namespace kura::lsp {
namespace {
auto IsKeyword(const std::string_view name) -> bool {
#define CHECK_KEYWORD(Name, Text) \
  if (name == Text)               \
    return true;
  FOR_EACH_KEYWORD_TOKEN(CHECK_KEYWORD)
#undef CHECK_KEYWORD
  return false;
}

auto IsIdentChar(const char c) -> bool {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
}

// The first letter is what tells the parser a `Name` from a `name`, so a
// rename can't change it: a type or a variant stays capitalised, everything
// else stays lowercase.
auto ValidateName(const DeclKind kind, const std::string& name) -> std::optional<std::string> {
  if (name.empty())
    return "the new name is empty";

  if (name.front() == '`') {
    if (name.size() < 3 || name.back() != '`' || name.find('`', 1) != name.size() - 1)
      return "a name in backticks needs a closing backtick and nothing inside it but the name";
    return std::nullopt;
  }

  const auto first = static_cast<unsigned char>(name.front());
  if (!std::isalpha(first) && first != '_')
    return std::format("`{}` doesn't start with a letter", name);
  if (!std::all_of(name.begin(), name.end(), IsIdentChar))
    return std::format("`{}` has characters that aren't allowed in a name; letters, digits, `_` and `-` are", name);
  if (name.back() == '-')
    return std::format("`{}` ends with a `-`", name);
  if (IsKeyword(name))
    return std::format("`{}` is a keyword", name);

  const bool is_type = GetNamespace(kind) == Ns::kType;
  if (is_type && !std::isupper(first))
    return std::format("types and variants start with a capital letter, like `{}{}`", static_cast<char>(std::toupper(first)),
                       name.substr(1));
  if (!is_type && std::isupper(first))
    return std::format("only types and variants start with a capital letter; `{}{}` would be read as a variant",
                       static_cast<char>(std::tolower(first)), name.substr(1));
  return std::nullopt;
}

auto Spell(const std::string& name, const bool raw) -> std::string {
  if (name.front() == '`')
    return name;
  return raw ? "`" + name + "`" : name;
}
}  // namespace

auto GetDefinition(Document& doc, const Position& pos, ModuleResolver* modules) -> std::optional<Target> {
  const auto* bindings = doc.GetBindings();
  if (!bindings)
    return std::nullopt;

  const auto offset = doc.ToOffset(pos);
  if (const auto* occurrence = bindings->FindOccurrenceAt(offset)) {
    return Target{doc.GetUri(), doc.ToRange(bindings->GetDecl(occurrence->decl).name_span)};
  }

  if (modules) {
    if (const auto* qualified = bindings->FindQualifiedRefAt(offset)) {
      if (const auto target = ResolveQualified(*modules, doc, *qualified))
        return Target{target->doc->GetUri(), target->doc->ToRange(target->decl->name_span)};
    }
  }
  return std::nullopt;
}

auto GetReferences(Document& doc, const Position& pos, const bool include_declaration) -> std::vector<Range> {
  std::vector<Range> result{};
  const auto* bindings = doc.GetBindings();
  if (!bindings)
    return result;

  const auto* occurrence = bindings->FindOccurrenceAt(doc.ToOffset(pos));
  if (!occurrence)
    return result;

  // The text of a pun is two things at once; list it once.
  std::set<std::pair<uint32_t, uint32_t>> seen{};
  for (const auto* o : bindings->GetOccurrences(occurrence->decl, true)) {
    if (o->is_declaration && !include_declaration)
      continue;
    if (!seen.insert({o->span.begin, o->span.end}).second)
      continue;
    result.push_back(doc.ToRange(o->span));
  }
  return result;
}

auto GetHighlights(Document& doc, const Position& pos) -> std::vector<Highlight> {
  std::vector<Highlight> result{};
  const auto* bindings = doc.GetBindings();
  if (!bindings)
    return result;

  const auto* occurrence = bindings->FindOccurrenceAt(doc.ToOffset(pos));
  if (!occurrence)
    return result;

  std::set<std::pair<uint32_t, uint32_t>> seen{};
  for (const auto* o : bindings->GetOccurrences(occurrence->decl, true)) {
    if (!seen.insert({o->span.begin, o->span.end}).second)
      continue;
    result.push_back(Highlight{
        .range = doc.ToRange(o->span),
        .kind = o->is_declaration ? Highlight::kWrite : Highlight::kRead,
    });
  }
  return result;
}

namespace {
struct Renameable {
  const Bindings* bindings = nullptr;
  const Decl* decl = nullptr;
  const Occurrence* occurrence = nullptr;
};

auto FindRenameable(Document& doc, const Position& pos) -> std::expected<Renameable, std::string> {
  const auto* bindings = doc.GetBindings();
  if (!bindings)
    return std::unexpected("the file has not been parsed");

  const auto* occurrence = bindings->FindOccurrenceAt(doc.ToOffset(pos));
  if (!occurrence)
    return std::unexpected("there is no name here that is declared in this file");

  const auto& decl = bindings->GetDecl(occurrence->decl);
  if (decl.implicit) {
    return std::unexpected(
        std::format("`{}` is the name Kura gives the header; write `as <name>` after the `use` to choose one", decl.name));
  }

  if (decl.kind == DeclKind::kField) {
    // Fields are found by the type of what is in front of the `.`. Where that type
    // can't be worked out, a use can't be tied to the field, and renaming only the
    // ones that can would break the rest.
    for (const auto& o : bindings->GetOccurrences()) {
      if (o.is_guess && bindings->GetDecl(o.decl).name == decl.name) {
        return std::unexpected(std::format(
            "some uses of `{}` can't be tied to this field because the type of the value they read from is not known", decl.name));
      }
    }
    // `{ width }` in a pattern is the field and a new binding at once. When the
    // record is not known, the field half of it was not recorded.
    for (const auto& o : bindings->GetOccurrences()) {
      if (!o.is_declaration || o.pun != Pun::kPattern || bindings->GetDecl(o.decl).name != decl.name)
        continue;
      const bool tied = std::any_of(bindings->GetOccurrences().begin(), bindings->GetOccurrences().end(),
                                    [&](const Occurrence& other) { return other.span == o.span && other.decl == decl.id; });
      if (!tied) {
        return std::unexpected(
            std::format("`{}` is also taken apart in a pattern whose type is not known, which can't be followed", decl.name));
      }
    }
  }

  return Renameable{bindings, &decl, occurrence};
}
}  // namespace

auto PrepareRename(Document& doc, const Position& pos) -> std::expected<RenameTarget, std::string> {
  const auto target = FindRenameable(doc, pos);
  if (!target)
    return std::unexpected(target.error());

  return RenameTarget{
      .range = doc.ToRange(target->occurrence->span),
      .placeholder = target->decl->name,
  };
}

auto Rename(Document& doc, const Position& pos, const std::string& new_name)
    -> std::expected<std::vector<TextEdit>, std::string> {
  const auto target = FindRenameable(doc, pos);
  if (!target)
    return std::unexpected(target.error());

  const auto& bindings = *target->bindings;
  const auto& decl = *target->decl;
  if (const auto problem = ValidateName(decl.kind, new_name))
    return std::unexpected(*problem);

  const auto bare = new_name.front() == '`' ? new_name.substr(1, new_name.size() - 2) : new_name;
  if (bare == decl.name)
    return std::vector<TextEdit>{};

  // A name that is already taken where this one lives would merge the two.
  if (decl.kind == DeclKind::kField) {
    for (const auto* sibling : bindings.GetChildren(static_cast<uint32_t>(decl.parent))) {
      if (sibling->kind == DeclKind::kField && sibling->name == bare && sibling->id != decl.id)
        return std::unexpected(std::format("the type already has a field called `{}`", bare));
    }
  } else {
    for (const auto id : bindings.GetScopes()[decl.scope].decls) {
      const auto& other = bindings.GetDecl(id);
      if (other.id != decl.id && other.name == bare && GetNamespace(other.kind) == GetNamespace(decl.kind))
        return std::unexpected(std::format("`{}` is already declared in the same place", bare));
    }
  }

  const auto spelled = Spell(new_name, decl.raw);
  std::vector<TextEdit> edits{};
  std::set<std::tuple<uint32_t, uint32_t, std::string>> seen{};
  const auto add = [&](const Span span, const std::string& text) {
    if (!seen.insert({span.begin, span.end, text}).second)
      return;
    edits.push_back(TextEdit{.range = doc.ToRange(span), .newText = text});
  };

  const auto old_spelling = decl.raw ? "`" + decl.name + "`" : decl.name;
  for (const auto* o : bindings.GetOccurrences(decl.id, false)) {
    const bool is_field = decl.kind == DeclKind::kField;
    switch (o->pun) {
      case Pun::kArg:
        if (is_field) {
          // `{ width: }` is `{ width: width }`; only the field half changes.
          add(o->span, spelled);
          add(Span(o->pun_end, o->pun_end), " " + old_spelling);
        } else {
          add(Span(o->pun_end, o->pun_end), " " + spelled);
        }
        break;
      case Pun::kPattern:
        // `{ width }` is `{ width: width }`.
        add(o->span, is_field ? spelled + ": " + old_spelling : old_spelling + ": " + spelled);
        break;
      case Pun::kNone:
        add(o->span, spelled);
        break;
    }
  }

  std::sort(edits.begin(), edits.end(), [](const TextEdit& a, const TextEdit& b) {
    return std::tie(a.range.start.line, a.range.start.character) < std::tie(b.range.start.line, b.range.start.character);
  });
  return edits;
}
}  // namespace kura::lsp
