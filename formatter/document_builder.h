#ifndef KURA_DOCUMENT_BUILDER_H
#define KURA_DOCUMENT_BUILDER_H

#include "formatter/config.h"
#include "formatter/document.h"

namespace kura::fmt {
class DocumentBuilder {
 private:
  Config config_{};

  inline auto ShouldOrderTypesFirst() const -> bool {
    return config_.types_first;
  }

  inline auto GetTabWidth() const -> int {
    return config_.indent_size;
  }

#define DECLARE_LOWER_FN(Name) auto Lower##Name(const syntax::Name& rhs) -> NodePtr
  DECLARE_LOWER_FN(Directive);
  DECLARE_LOWER_FN(Module);
  DECLARE_LOWER_FN(Binding);
  DECLARE_LOWER_FN(UseDecl);
  DECLARE_LOWER_FN(FnDecl);
  DECLARE_LOWER_FN(UnitLit);
  DECLARE_LOWER_FN(NumberLit);
  DECLARE_LOWER_FN(NoneLit);
  DECLARE_LOWER_FN(SeqLit);
  DECLARE_LOWER_FN(StringLit);
  DECLARE_LOWER_FN(RecordLit);
  DECLARE_LOWER_FN(TypeDecl);
  DECLARE_LOWER_FN(VariantType);
  DECLARE_LOWER_FN(RecordType);
  DECLARE_LOWER_FN(UnionType);
  DECLARE_LOWER_FN(NamedType);
  DECLARE_LOWER_FN(FunctionType);
  DECLARE_LOWER_FN(NoneType);
  DECLARE_LOWER_FN(Field);
  DECLARE_LOWER_FN(Call);
  DECLARE_LOWER_FN(Param);
  DECLARE_LOWER_FN(VarRef);
  DECLARE_LOWER_FN(Member);
  DECLARE_LOWER_FN(Binary);
  DECLARE_LOWER_FN(Unary);
  DECLARE_LOWER_FN(Arg);
  DECLARE_LOWER_FN(MatchExpr);
  DECLARE_LOWER_FN(MatchArm);
  DECLARE_LOWER_FN(LiteralPattern);
  DECLARE_LOWER_FN(NonePattern);
  DECLARE_LOWER_FN(WildcardPattern);
  DECLARE_LOWER_FN(Block);
  DECLARE_LOWER_FN(Pipe);
  DECLARE_LOWER_FN(ForExpr);
  DECLARE_LOWER_FN(Paren);
  DECLARE_LOWER_FN(BoolLit);
  DECLARE_LOWER_FN(Index);
  DECLARE_LOWER_FN(RangeExpr);
  DECLARE_LOWER_FN(RangePattern);
  DECLARE_LOWER_FN(RecordPattern);
  DECLARE_LOWER_FN(FieldPattern);
  DECLARE_LOWER_FN(QualifiedType);
  DECLARE_LOWER_FN(StaticAssert);
  DECLARE_LOWER_FN(TestDecl);
  DECLARE_LOWER_FN(ExternDecl);
  DECLARE_LOWER_FN(IsExpr);
  DECLARE_LOWER_FN(VariantPattern);
  DECLARE_LOWER_FN(VariantRef);
  DECLARE_LOWER_FN(Lambda);
  DECLARE_LOWER_FN(UiElement);
  DECLARE_LOWER_FN(WhenExpr);
  DECLARE_LOWER_FN(IfExpr);
  DECLARE_LOWER_FN(Placeholder);
  DECLARE_LOWER_FN(BindingPattern);
  DECLARE_LOWER_FN(StringTemplate);
  DECLARE_LOWER_FN(RecordUpdate);
  DECLARE_LOWER_FN(SetRule);
  DECLARE_LOWER_FN(UpdateEntry);
#undef DECLARE_LOWER_FN

 public:
  explicit DocumentBuilder(const Config config = {}) :
    config_(std::move(config)) {}
  ~DocumentBuilder() = default;
  auto Lower(const syntax::Node& node) -> NodePtr;
};
}  // namespace kura::fmt

#endif  // KURA_DOCUMENT_BUILDER_H
