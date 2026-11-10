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
    return linebreak();
  }

  inline auto heading(const std::string text) -> hover_info_stream& {
    return heading(1, std::move(text));
  }

  inline auto heading(const int level, const syntax::Name& rhs) -> hover_info_stream& {
    return heading(level, rhs.text);
  }

  inline auto heading(const syntax::Name& rhs) -> hover_info_stream& {
    return heading(1, rhs);
  }

  inline auto line() -> hover_info_stream& {
    stream() << "---";
    return linebreak();
  }

  inline auto linebreak(const int num = 1) -> hover_info_stream& {
    for (auto i = 0; i < num; i++)
      stream() << "\n";
    return *this;
  }

  inline auto operator<<(const std::string_view rhs) -> hover_info_stream& {
    stream() << rhs;
    return *this;
  }

  inline auto operator<<(const syntax::FnDecl& fn) -> hover_info_stream& {
    heading(fn.name);
    line();
    if (!fn.params.empty()) {
      stream() << "Parameters:\n";
      for (const auto& param : fn.params) {
        const auto& p = param->As<syntax::Param>();
        const auto& name = p->name.text;
        const auto& type = p->type->As<syntax::NamedType>()->name.text;
        stream() << " - " << type << " " << name << "\n";
      }
    }
    linebreak();

    if (fn.result)
      stream() << "Return Type: " << fn.result->GetKindName() << "\n";
    return *this;
  }

  inline auto operator<<(const syntax::Param& rhs) -> hover_info_stream& {
    heading(rhs.name);
    line();
    const auto* type = rhs.type->As<syntax::NamedType>();
    stream() << "Type: " << type->name.text << "\n";
    return *this;
  }

  inline auto operator<<(const syntax::Binding& rhs) -> hover_info_stream& {
    heading(rhs.name);
    line();
    return *this;
  }

  inline auto operator<<(const syntax::TypeDecl& rhs) -> hover_info_stream& {
    heading(rhs.name);
    line();
    stream() << rhs.type->GetKindName() << "\n";
    if (const auto* type = rhs.type->As<syntax::NamedType>()) {
      stream() << "Args:";
      linebreak();
      for (const auto& arg : type->args) {
        stream() << " - " << arg->GetKindName();
        linebreak();
      }
    } else if (const auto* type = rhs.type->As<syntax::VariantType>()) {
    } else if (const auto* type = rhs.type->As<syntax::UnionType>()) {
      stream() << "Types:";
      linebreak();
      for (const auto& member : type->members) {
        stream() << " - " << member->GetKindName();
        linebreak();
      }
      linebreak();
    } else if (const auto* type = rhs.type->As<syntax::RecordType>()) {
      stream() << "Properties:";
      linebreak();
      for (const auto& f : type->fields) {
        const auto* field = f->As<syntax::Field>();
        stream() << " - " << field->name.text << " ";
        if (const auto* namedType = field->type->As<syntax::NamedType>())
          stream() << namedType->name.text;

        linebreak();
      }
      linebreak();
    }

    return *this;
  }

  inline auto operator<<(const syntax::UiElement& rhs) -> hover_info_stream& {
    heading(rhs.tag.text);
    line();

    if (rhs.has_props) {
      stream() << "Properties:\n";
      for (const auto& prop : rhs.props) {
        const auto& a = prop->As<syntax::Arg>();
        stream() << " - " << a->name.text << " ";
        if (const auto& str = a->value->As<syntax::StringLit>()) {
          stream() << str->value << " (StringLit)";
        } else if (const auto& b = a->value->As<syntax::BoolLit>()) {
          stream() << (b->value ? "true" : "false") << " (BoolLit)";
        } else if (const auto& n = a->value->As<syntax::NumberLit>()) {
          stream() << n->text << " (NumberLit)";
        } else if (const auto& u = a->value->As<syntax::UnitLit>()) {
          stream() << u->text << " (UnitLit)";
        } else if (const auto& n = a->value->As<syntax::NoneLit>()) {
          stream() << "None" << " (NoneLit)";
        }

        linebreak();
      }
    }

    return *this;
  }

  inline auto operator<<(const syntax::Node& rhs) -> hover_info_stream& {
    if (const auto* fn = rhs.As<syntax::FnDecl>()) {
      operator<<(*fn);
    } else if (const auto* param = rhs.As<syntax::Param>()) {
      operator<<(*param);
    } else if (const auto* binding = rhs.As<syntax::Binding>()) {
      operator<<(*binding);
    } else if (const auto* e = rhs.As<syntax::UiElement>()) {
      operator<<(*e);
    } else if (const auto* t = rhs.As<syntax::TypeDecl>()) {
      operator<<(*t);
    }

    return linebreak(2);
  }
};
}  // namespace kura::lsp

#endif  // KURA_LSP_HOVER_H
