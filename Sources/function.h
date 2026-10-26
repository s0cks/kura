#ifndef KURA_FUNCTION_H
#define KURA_FUNCTION_H

#include <string>

#include <vector>

#include "common.h"
#include "c_type.h"
#include "frontend/type_expr.h"
#include "object.h"

namespace kura {
class GraphEntryInstr;
class LocalVariable;
namespace expr {
class Expr;
class SeqExpr;
class ExprBuilder;
}  // namespace expr

class Function {
  friend class expr::ExprBuilder;
  DEFINE_NON_COPYABLE_TYPE(Function);

 public:
  // `name: Type`. The annotation is syntax only until types get resolved.
  struct Parameter {
    LocalVariable* local;
    expr::TypeExpr* type;
  };
  using ParameterList = std::vector<Parameter>;

 private:
  String* name_;
  expr::SeqExpr* body_ = nullptr;
  GraphEntryInstr* entry_ = nullptr;
  ParameterList parameters_{};
  expr::TypeExpr* return_type_ = nullptr;
  expr::DirectiveList directives_{};
  bool c_export_ = false;
  CSignature* c_signature_ = nullptr;

  void SetCSignature(CSignature* rhs) {
    c_signature_ = rhs;
  }

  void SetCExport(const bool rhs) {
    c_export_ = rhs;
  }

  void SetBody(expr::SeqExpr* rhs) {
    body_ = rhs;
  }

  void SetParameters(const ParameterList rhs) {
    parameters_ = std::move(rhs);
  }

  void SetReturnType(expr::TypeExpr* rhs) {
    return_type_ = rhs;
  }

  void SetDirectives(const expr::DirectiveList rhs) {
    directives_ = std::move(rhs);
  }

 public:
  explicit Function(String* name) :
    name_(name) {}
  ~Function() = default;

  auto GetName() const -> String* {
    return name_;
  }

  auto GetBody() const -> expr::SeqExpr* {
    return body_;
  }

  inline auto HasBody() const -> bool {
    return GetBody() != nullptr;
  }

  auto GetParameters() const -> const ParameterList& {
    return parameters_;
  }

  // Null when the function has no return type annotation.
  auto GetReturnType() const -> expr::TypeExpr* {
    return return_type_;
  }

  auto GetDirectives() const -> const expr::DirectiveList& {
    return directives_;
  }

  // Declared `extern fn name(...): T => body`: callable from C under the C ABI.
  auto IsCExport() const -> bool {
    return c_export_;
  }

  // The C signature of an `extern fn` (an import has no body, an export has one).
  auto GetCSignature() const -> CSignature* {
    return c_signature_;
  }

  // Declared `extern fn name(...): T` with no body: the code is in C.
  auto IsExtern() const -> bool {
    return c_signature_ != nullptr && !c_export_;
  }

  auto GetEntry() const -> GraphEntryInstr* {
    return entry_;
  }

  inline auto HasEntry() const -> bool {
    return GetEntry() != nullptr;
  }

  auto ToString() const -> std::string;

 public:
  static inline auto New(String* name) -> Function* {
    return new Function(name);
  }

  static inline auto New(const std::string name) -> Function* {
    return New(String::New(std::move(name)));
  }
};
}  // namespace kura

#endif  // KURA_FUNCTION_H
