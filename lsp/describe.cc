#include "lsp/describe.h"

#include <format>
#include <sstream>

namespace kura::lsp {
using syntax::Node;

auto TypeToString(const Node* type) -> std::string {
  if (!type)
    return "?";

  const auto list = [](const std::vector<Node*>& nodes) {
    std::string out{};
    for (size_t i = 0; i < nodes.size(); i++)
      out += (i ? ", " : "") + TypeToString(nodes[i]);
    return out;
  };

  switch (type->GetKind()) {
    case Node::kNamedType: {
      const auto* t = type->As<syntax::NamedType>();
      return t->args.empty() ? t->name.text : std::format("{}<{}>", t->name.text, list(t->args));
    }
    case Node::kQualifiedType: {
      const auto* t = type->As<syntax::QualifiedType>();
      const auto base = std::format("{}.{}", t->qualifier.text, t->member.text);
      return t->args.empty() ? base : std::format("{}<{}>", base, list(t->args));
    }
    case Node::kVariantType: {
      const auto* t = type->As<syntax::VariantType>();
      return t->payload ? std::format("{}({})", t->name.text, TypeToString(t->payload)) : t->name.text;
    }
    case Node::kNoneType:
      return "None";
    case Node::kRecordType: {
      const auto* t = type->As<syntax::RecordType>();
      std::string out = "{ ";
      for (size_t i = 0; i < t->fields.size(); i++) {
        const auto* f = t->fields[i]->As<syntax::Field>();
        if (!f)
          continue;
        out += std::format("{}{}{}: {}", i ? ", " : "", f->name.text, f->optional ? "?" : "", TypeToString(f->type));
      }
      return out + " }";
    }
    case Node::kFunctionType: {
      const auto* t = type->As<syntax::FunctionType>();
      return std::format("({}) => {}", list(t->params), TypeToString(t->result));
    }
    case Node::kUnionType: {
      const auto* t = type->As<syntax::UnionType>();
      std::string out{};
      for (size_t i = 0; i < t->members.size(); i++)
        out += (i ? " | " : "") + TypeToString(t->members[i]);
      return out;
    }
    default:
      return "?";
  }
}

auto ParamsToString(const std::vector<Node*>& params, const bool variadic) -> std::string {
  std::string out = "(";
  for (size_t i = 0; i < params.size(); i++) {
    const auto* p = params[i]->As<syntax::Param>();
    if (!p)
      continue;
    out += std::format("{}{}: {}", i ? ", " : "", p->name.text, TypeToString(p->type));
  }
  if (variadic)
    out += params.empty() ? "..." : ", ...";
  return out + ")";
}

auto SignatureOf(const syntax::FnDecl& fn) -> std::string {
  auto out = std::format("fn {}{}", fn.name.text, ParamsToString(fn.params));
  if (fn.result)
    out += ": " + TypeToString(fn.result);
  return out;
}

auto SignatureOf(const syntax::ExternDecl& ext) -> std::string {
  auto out = std::format("extern \"{}\" fn {}{}", ext.symbol, ext.c_name.text, ParamsToString(ext.params, ext.variadic));
  if (ext.result)
    out += ": " + TypeToString(ext.result);
  return out;
}

auto DeclarationOf(const syntax::TypeDecl& type) -> std::string {
  std::string params{};
  if (!type.type_params.empty()) {
    params = "<";
    for (size_t i = 0; i < type.type_params.size(); i++)
      params += (i ? ", " : "") + type.type_params[i].text;
    params += ">";
  }
  const auto head = std::format("type {}{} = ", type.name.text, params);

  if (const auto* record = type.type ? type.type->As<syntax::RecordType>() : nullptr) {
    std::string out = head + "{\n";
    for (const auto* f : record->fields) {
      const auto* field = f->As<syntax::Field>();
      if (field)
        out += std::format("  {}{}: {},\n", field->name.text, field->optional ? "?" : "", TypeToString(field->type));
    }
    return out + "}";
  }

  const auto body = TypeToString(type.type);
  if (head.size() + body.size() <= 80 || !type.type || !type.type->Is<syntax::UnionType>())
    return head + body;

  std::string out = head.substr(0, head.size() - 1) + "\n";
  for (const auto* member : type.type->As<syntax::UnionType>()->members)
    out += "  | " + TypeToString(member) + "\n";
  out.pop_back();
  return out;
}

auto LeadingComment(const std::string_view text, const uint32_t offset) -> std::string {
  if (offset > text.size())
    return {};

  auto line_start = text.rfind('\n', offset == 0 ? 0 : offset - 1);
  line_start = line_start == std::string_view::npos ? 0 : line_start + 1;
  // A declaration may start in the middle of a line (after its directives); the
  // comment belongs to the line it starts on.
  std::vector<std::string> lines{};
  size_t cursor = line_start;
  while (cursor > 0) {
    const auto prev_end = cursor - 1;
    const auto prev_start_pos = text.rfind('\n', prev_end == 0 ? 0 : prev_end - 1);
    const auto prev_start = prev_start_pos == std::string_view::npos || prev_end == 0 ? 0 : prev_start_pos + 1;
    auto line = text.substr(prev_start, prev_end - prev_start);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
      line.remove_suffix(1);
    const auto first = line.find_first_not_of(" \t");
    if (first == std::string_view::npos || !line.substr(first).starts_with("//"))
      break;

    auto content = line.substr(first + 2);
    if (content.starts_with("/"))
      content.remove_prefix(1);
    if (content.starts_with(" "))
      content.remove_prefix(1);
    lines.emplace_back(content);
    cursor = prev_start;
  }

  std::string out{};
  for (auto it = lines.rbegin(); it != lines.rend(); ++it)
    out += (out.empty() ? "" : "\n") + *it;
  return out;
}

auto DescribeBuiltinType(const std::string_view name) -> std::string_view {
  if (name == "Bool")
    return "`true` or `false`.";
  if (name == "None")
    return "The absence of a value. `Optional<T>` is `T | None`.";
  if (name == "Int")
    return "A 64-bit signed integer.";
  if (name == "Float")
    return "A 64-bit floating point number.";
  if (name == "Number")
    return "An `Int` or a `Float`.";
  if (name == "String")
    return "Text.";
  if (name == "Color" || name == "Rgba")
    return "A color with red, green, blue and alpha channels.";
  if (name == "Hsla")
    return "A color with hue, saturation, lightness and alpha.";
  if (name == "Hsba" || name == "Hsva")
    return "A color with hue, saturation, brightness and alpha.";
  if (name == "Vec2")
    return "A pair of floats.";
  if (name == "Vec3")
    return "Three floats.";
  if (name == "Rect")
    return "A position and a size.";
  if (name == "Px")
    return "A length in pixels, written `10px`.";
  if (name == "Pct")
    return "A share of the parent, written `50%`.";
  if (name == "Fr")
    return "A share of the space that is left, written `1fr`.";
  if (name == "Node")
    return "Something that can be drawn: an element made with `#tag`.";
  if (name == "Seq")
    return "A sequence of values: `Seq<T>`.";
  if (name == "Optional")
    return "A `T` or `None`: `Optional<T>`.";
  if (name == "Size")
    return "A record of `width: Float` and `height: Float`; what `Resized` carries.";
  if (name == "SystemMessage")
    return "The messages the host sends: `Resized(Size)` and `Focused(Bool)`.";
  return {};
}

auto DescribeDecl(const Bindings& bindings, const Decl& decl, const std::string_view text) -> std::string {
  std::string code{};
  std::string note{};
  const Node* node = decl.node;

  const auto parent_name = [&]() -> std::string {
    return decl.parent >= 0 ? bindings.GetDecl(static_cast<uint32_t>(decl.parent)).name : std::string{};
  };

  switch (decl.kind) {
    case DeclKind::kFn:
      code = SignatureOf(*node->As<syntax::FnDecl>());
      break;
    case DeclKind::kExtern:
      code = SignatureOf(*node->As<syntax::ExternDecl>());
      note = "Defined in C; called through the FFI.";
      break;
    case DeclKind::kType:
      code = DeclarationOf(*node->As<syntax::TypeDecl>());
      break;
    case DeclKind::kVariant: {
      const auto* v = node->As<syntax::VariantType>();
      code = v ? TypeToString(v) : decl.name;
      note = std::format("A variant of `{}`.", parent_name());
      break;
    }
    case DeclKind::kField: {
      const auto* f = node->As<syntax::Field>();
      code = std::format("{}{}: {}", decl.name, f && f->optional ? "?" : "", f ? TypeToString(f->type) : "?");
      note = std::format("A field of `{}`.", parent_name());
      break;
    }
    case DeclKind::kParam: {
      const auto* p = node->As<syntax::Param>();
      code = std::format("{}: {}", decl.name, p ? TypeToString(p->type) : "?");
      if (!parent_name().empty())
        note = std::format("A parameter of `{}`.", parent_name());
      break;
    }
    case DeclKind::kAlias: {
      const auto* use = node->As<syntax::UseDecl>();
      code = std::format("use \"{}\" as {}", use->header, decl.name);
      note = "A C header; its functions and types are reached as `" + decl.name + ".name`.";
      break;
    }
    case DeclKind::kLet: {
      const auto* b = node->As<syntax::Binding>();
      const auto* type = bindings.GetDeclType(decl.id);
      code = (b && b->is_let ? "let " : "") + decl.name + (type ? ": " + TypeToString(type) : "");
      break;
    }
    default: {
      const auto* type = bindings.GetDeclType(decl.id);
      code = decl.name + (type ? ": " + TypeToString(type) : "");
      note = std::format("A {}.", ToString(decl.kind));
      break;
    }
  }

  std::string out = "```kura\n" + code + "\n```";
  const auto comment = LeadingComment(text, decl.span.begin);
  if (!comment.empty())
    out += "\n\n" + comment;
  if (!note.empty())
    out += "\n\n*" + note + "*";
  return out;
}
}  // namespace kura::lsp
