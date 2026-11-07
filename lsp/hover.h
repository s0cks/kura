#ifndef KURA_LSP_HOVER_H
#define KURA_LSP_HOVER_H

#include <sstream>

#include "frontend/syntax/ast.h"
#include "frontend/syntax/parser.h"

namespace kura::lsp {
class hover_info_stream {
 private:
  std::ostream& stream_;

 public:
  hover_info_stream(std::ostream& stream) :
    stream_(stream) {}
  ~hover_info_stream() = default;

  inline auto stream() -> std::ostream& {
    return stream_;
  }

  inline auto heading(const uint8_t level, const std::string text) -> hover_info_stream& {
    for (uint8_t i = 0; i < (level + 1); i++)
      stream() << "#";
    stream() << " ";
    stream() << text;
    stream() << "\n";
    return *this;
  }

  inline auto heading(const std::string text) -> hover_info_stream& {
    return heading(1, std::move(text));
  }

  inline auto line() -> hover_info_stream& {
    stream() << "---\n";
    return *this;
  }

  inline auto operator<<(const std::string_view rhs) -> hover_info_stream& {
    stream() << rhs;
    return *this;
  }
};

static inline void ToHoverInfo(hover_info_stream& stream, const syntax::FnDecl& fn) {
  stream.heading(fn.name.text);
  stream.line();
  stream << "Paramters:\n";
  if (!fn.params.empty()) {
    for (const auto& param : fn.params) {
      const auto& p = param->As<syntax::Param>();
      const auto& name = p->name.text;
      const auto& type = p->type->As<syntax::NamedType>()->name.text;
      stream << " - " << type << " " << name << "\n";
    }
  }
  stream << "Return Type: " << fn.result->GetKindName() << "\n";
  stream << "\n\n";
}

static inline void ToHoverInfo(hover_info_stream& stream, const syntax::Param& param) {
  stream.heading(param.name.text);
  stream.line();
  const auto* type = param.type->As<syntax::NamedType>();
  stream << "Type: " << type->name.text << "\n";
  stream << "\n\n";
}

static inline void ToHoverInfo(hover_info_stream& stream, const syntax::Binding& binding) {
  stream.heading(binding.name.text);
  stream.line();
  stream << "\n\n";
}

static inline void ToHoverInfo(hover_info_stream& stream, const syntax::UiElement& e) {
  stream.heading(e.tag.text);
  stream.line();
  stream << "Properties:\n";
  for (const auto& prop : e.props) {
    const auto& a = prop->As<syntax::Arg>();
    stream << " - " << a->name.text << " ";
    if (const auto& str = a->value->As<syntax::StringLit>()) {
      stream << str->value << " (StringLit)";
    } else if (const auto& b = a->value->As<syntax::BoolLit>()) {
      stream << (b->value ? "true" : "false") << " (BoolLit)";
    } else if (const auto& n = a->value->As<syntax::NumberLit>()) {
      stream << n->text << " (NumberLit)";
    } else if (const auto& u = a->value->As<syntax::UnitLit>()) {
      stream << u->text << " (UnitLit)";
    } else if (const auto& n = a->value->As<syntax::NoneLit>()) {
      stream << "None" << " (NoneLit)";
    }
    stream << "\n";
  }
  stream << "\n\n";
}

static inline void ToHoverInfo(hover_info_stream& ss, const syntax::Node& node) {
  if (const auto* fn = node.As<syntax::FnDecl>()) {
    return ToHoverInfo(ss, *fn);
  } else if (const auto* param = node.As<syntax::Param>()) {
    return ToHoverInfo(ss, *param);
  } else if (const auto* binding = node.As<syntax::Binding>()) {
    return ToHoverInfo(ss, *binding);
  } else if (const auto* e = node.As<syntax::UiElement>()) {
    return ToHoverInfo(ss, *e);
  }
}

static inline void ToHoverInfo(std::ostream& os, const syntax::Node& rhs) {
  hover_info_stream ss(os);
  return ToHoverInfo(ss, rhs);
}
}  // namespace kura::lsp

#endif  // KURA_LSP_HOVER_H
