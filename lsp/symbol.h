#ifndef KURA_LSP_SYMBOL_H
#define KURA_LSP_SYMBOL_H

#include <cstdint>
#include <string>

#include "lsp/types.h"

namespace kura::lsp {
#define FOR_EACH_SYMBOL_KIND(V) \
  V(FileSymbol)                 \
  V(ModuleSymbol)               \
  V(NamespaceSymbol)            \
  V(PackageSymbol)              \
  V(ClassSymbol)                \
  V(MethodSymbol)               \
  V(PropertySymbol)             \
  V(FieldSymbol)                \
  V(ConstructorSymbol)          \
  V(EnumSymbol)                 \
  V(InterfaceSymbol)            \
  V(FunctionSymbol)             \
  V(VariableSymbol)             \
  V(ConstantSymbol)             \
  V(StringSymbol)               \
  V(NumberSymbol)               \
  V(BooleanSymbol)              \
  V(ArraySymbol)                \
  V(ObjectSymbol)               \
  V(KeySymbol)                  \
  V(NullSymbol)                 \
  V(EnumMemberSymbol)           \
  V(StructSymbol)               \
  V(EventSymbol)                \
  V(OperatorSymbol)             \
  V(TypeParameterSymbol)

struct Symbol {
 public:
  // clang-format off
  enum Kind : uint32_t {
    kInvalidSymbol = 0,
#define DEFINE_SYMBOL(Name) k##Name,
    FOR_EACH_SYMBOL_KIND(DEFINE_SYMBOL)
#undef DEFINE_SYMBOL
    kTotalNumberOfKinds,
  };
  // clang-format on

  std::string name;
  std::string detail;
  Kind kind;
  Range range;
  Range selectionRange;

#define DEFINE_KIND_CHECK(Name)          \
  inline auto Is##Name() const -> bool { \
    return kind == Kind::k##Name;        \
  }

  FOR_EACH_SYMBOL_KIND(DEFINE_KIND_CHECK)
#undef DEFINE_KIND_CHECK

  void operator>>(json_t* doc) const;
  auto operator<<(const json_t* doc) -> Symbol&;

  friend auto operator<<(std::ostream& stream, const Symbol& rhs) -> std::ostream& {
    stream << "Symbol{";
    stream << "name=" << rhs.name << ", ";
    stream << "detail=" << rhs.detail << ", ";
    stream << "kind=" << rhs.kind << ", ";
    stream << "range=" << rhs.range << ", ";
    stream << "selectionRange=" << rhs.selectionRange;
    stream << ")";
    return stream;
  }
};
}  // namespace kura::lsp

#endif  // KURA_LSP_SYMBOL_H
