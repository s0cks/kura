#include "frontend/builtins.h"

namespace kura {
namespace {
using namespace expr;

auto Named(const std::string& name) -> TypeExpr* {
  return new NamedTypeExpr(name);
}

auto Build() -> std::vector<TypeDecl*> {
  const auto size = new TypeDecl(
      "Size", {}, new RecordTypeExpr({{"width", Named("Float"), false}, {"height", Named("Float"), false}}));
  const auto messages =
      new TypeDecl("SystemMessage", {},
                   new UnionTypeExpr({new VariantTypeExpr("Resized", Named("Size")),
                                      new VariantTypeExpr("Focused", Named("Bool"))}));
  return {size, messages};
}
}  // namespace

auto BuiltinTypeDecls() -> const std::vector<TypeDecl*>& {
  // made once, and never freed: they are declarations every analysis shares
  static const auto decls = Build();
  return decls;
}

auto BuiltinMessages() -> const std::vector<BuiltinMessage>& {
  static const std::vector<BuiltinMessage> messages{{"Resized", "Size"}, {"Focused", "Bool"}};
  return messages;
}
}  // namespace kura
