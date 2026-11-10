#ifndef KURA_EXPR_BUILDER_H
#define KURA_EXPR_BUILDER_H

#include <functional>
#include <memory>
#include <stdexcept>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "c_import.h"
#include "c_type.h"
#include "frontend/c_decl.h"
#include "frontend/expr.h"
#include "frontend/syntax/ast.h"
#include "frontend/syntax/parser.h"
#include "function.h"
#include "local_scope.h"
#include "object.h"
#include "property.h"

namespace kura {
struct Module;
}

namespace kura::expr {
// Raised when the syntax tree is well formed but cannot be turned into an Expr
// tree: either the program is wrong (undefined name, ...) or it uses syntax the
// Expr IR has no node for yet (see `Unsupported`).
class BuildError : public std::runtime_error {
 private:
  size_t line_;
  size_t column_;
  syntax::Span span_;

 public:
  BuildError(const size_t line, const size_t column, const std::string& message, const syntax::Span& span = {}) :
    std::runtime_error(message),
    line_(line),
    column_(column),
    span_(span) {}
  ~BuildError() override = default;

  // One based, like a compiler prints it.
  auto GetLine() const -> size_t {
    return line_;
  }

  // Zero based, like it always was.
  auto GetColumn() const -> size_t {
    return column_;
  }

  // Where in the source the problem is, for tools that want to underline it.
  auto GetSpan() const -> const syntax::Span& {
    return span_;
  }

  auto ToString() const -> std::string {
    return std::to_string(line_) + ":" + std::to_string(column_) + ": " + what();
  }
};

class ExprBuilder;
class ModuleBuilder {
  friend class ExprBuilder;
  DEFINE_NON_COPYABLE_TYPE(ModuleBuilder);

 private:
  LocalScope* scope_;

  inline auto PushScope() -> LocalScope* {
    const auto new_scope = LocalScope::New(scope_);
    scope_ = new_scope;
    return new_scope;
  }

  inline auto PopScope() -> LocalScope* {
    if (scope_->IsRoot())
      return nullptr;
    const auto old_scope = scope_;
    scope_ = old_scope->GetParent();
    return old_scope;
  }

 public:
  explicit ModuleBuilder(LocalScope* scope) :
    scope_(scope) {}
  ~ModuleBuilder() = default;

  auto GetScope() const -> LocalScope* {
    return scope_;
  }

  // `tree` must be valid (no syntax errors). Throws BuildError.
  auto Build(const syntax::SyntaxTree& tree) -> Module*;

  auto operator()(const syntax::SyntaxTree& tree) -> Module* {
    return Build(tree);
  }
};

// Turns the v1 syntax tree into Expr nodes. This is where names are resolved,
// C headers are imported and the semantic rules of the language are checked;
// the syntax tree itself knows none of that.
class ExprBuilder {
 protected:
  class LocalsScope {
   private:
    ModuleBuilder* owner_;

   public:
    explicit LocalsScope(ModuleBuilder* owner) :
      owner_(owner) {
      GetOwner()->PushScope();
    }
    ~LocalsScope() {
      GetOwner()->PopScope();
    }

    auto GetOwner() const -> ModuleBuilder* {
      return owner_;
    }
  };

 private:
  ModuleBuilder* owner_;
  const syntax::SyntaxTree& tree_;
  LocalScope* module_scope_ = nullptr;
  std::unordered_set<std::string> function_names_{};
  std::unordered_set<std::string> use_aliases_{};
  std::unordered_map<std::string, std::shared_ptr<CHeader>> headers_{};
  std::unordered_set<std::string> imported_{};
  std::unordered_set<std::string> pure_headers_{};
  std::unordered_map<std::string, std::unordered_set<std::string>> pure_names_{};
  Module* module_ = nullptr;
  // The locals that hold the value of each enclosing `x |> stage-with-_`, innermost last. A `_` reads the
  // innermost one.
  std::vector<std::string> pipe_locals_{};
  // The parameter that stands for each `_` of a partial application being built.
  std::unordered_map<const syntax::Node*, std::string> placeholder_names_{};
  // The call whose placeholders are being turned into parameters.
  const syntax::Call* partial_ = nullptr;
  uint32_t pipe_count_ = 0;

  [[noreturn]] void Error(const syntax::Span& where, const std::string& message) const;
  [[noreturn]] void Error(const syntax::Node* where, const std::string& message) const;

  // An Int (`10`) or a Float (`10.0`).
  auto NumberLiteral(const syntax::Span& where, const std::string& text, const bool negate) const -> Object*;
  // Syntax that parses (it is part of v1) but that the Expr IR cannot represent yet.
  // Nothing in the core language is left in this state; it is only used for the
  // remaining unspecified corners (see the callers).
  [[noreturn]] void Unsupported(const syntax::Node* where, const std::string& what) const;

  auto Build(const syntax::Node* node) -> Expr*;
  auto BuildSeq(const syntax::Node* node) -> SeqExpr*;
  auto BuildBlock(const syntax::Block* block) -> SeqExpr*;
  auto BuildBinding(const syntax::Binding* binding) -> StoreLocalExpr*;
  auto BuildDirectives(const std::vector<syntax::Node*>& directives) -> DirectiveList;
  auto BuildType(const syntax::Node* node) -> TypeExpr*;
  auto BuildTypeDecl(const syntax::TypeDecl* decl) -> TypeDecl*;
  auto BuildUseDecl(const syntax::UseDecl* decl) -> UseDecl*;
  auto BuildCSignature(const syntax::ExternDecl* decl, const std::string& name) -> CSignature*;
  auto ToCType(const syntax::Node* where, TypeExpr* type, const bool is_result, const std::string& what) -> CType;
  auto ResolveImport(const syntax::Node* where, const std::string& alias, const std::string& member) -> Expr*;
  auto BuildUnit(const syntax::UnitLit* unit, const bool negate) const -> Object*;
  auto BuildCall(Expr* target, ExprList args) -> Expr*;
  auto BuildPipe(const syntax::Pipe* pipe) -> Expr*;
  auto BuildStagedPipe(const syntax::Pipe* pipe) -> Expr*;
  // `value |> callee(args)`: the call with the value in front of the arguments
  auto PipeInto(Expr* value, const syntax::Node* callee, const std::vector<syntax::Node*>& args) -> Expr*;
  // The stage of a pipe or of a composition applied to a value: `add(_, 1)`, `add(1)`, or a function.
  auto ApplyStage(Expr* value, const syntax::Node* stage, const syntax::Span& where) -> Expr*;
  auto BuildStageWith(Expr* value, const syntax::Node* stage, const syntax::Span& where) -> Expr*;
  // `add(_, 10)` is a function of the missing arguments; `f >> g` and `f << g` are functions of one
  auto BuildPartial(const syntax::Call* call) -> Expr*;
  auto BuildCompose(const syntax::Binary* binary) -> Expr*;
  auto BuildWhen(const syntax::WhenExpr* when) -> Expr*;
  // A string among the children of an element is `#text("...")`.
  auto BuildImplicitText(const syntax::Node* value) -> Expr*;
  // `a ?? b`, `x is pattern`: matches on a value that is computed once
  auto BuildCoalesce(const syntax::Binary* binary) -> Expr*;
  auto BuildIs(const syntax::IsExpr* is) -> Expr*;
  auto BuildIndex(const syntax::Index* index) -> Expr*;
  // `a?.[i]` and `a?.(args)`: None when `a` is, otherwise the access or call on `a` without its None. The
  // index or the arguments are only evaluated when `a` isn't None.
  auto BuildOptionalChain(const syntax::Node* object, const syntax::Node* where,
                          const std::function<Expr*(Expr* object)>& build) -> Expr*;
  // `let { a, b: c } = value` is a binding of the value followed by one binding per name
  auto BuildBindings(const syntax::Binding* binding) -> std::vector<StoreLocalExpr*>;
  void Destructure(const syntax::Node* pattern, const std::string& source, std::vector<StoreLocalExpr*>& out);
  // `{ p | x: 1, pos.y += 2 }` is a record that spreads `p` and sets the listed fields
  auto BuildRecordUpdate(const syntax::RecordUpdate* update) -> Expr*;
  auto BuildUpdateLevel(const syntax::Node* base, const std::vector<std::string>& prefix,
                        const std::vector<const syntax::UpdateEntry*>& entries) -> Expr*;
  // `f <| x`, and the shared part of `x |> stage` when the stage has a `_`
  auto BuildPipeBack(const syntax::PipeBack* back) -> Expr*;
  auto BuildPipedStage(const syntax::Node* value, const syntax::Node* stage, const syntax::Span& where) -> Expr*;
  void BuildStaticAssert(const syntax::StaticAssert* assertion);

  // A value the compiler can compute: what `static-assert` conditions are made of.
  struct Constant {
    enum Kind { kBool, kInt, kFloat, kString };
    Kind kind = kBool;
    bool boolean = false;
    int64_t integer = 0;
    double real = 0.0;
    std::string text{};
  };
  auto Fold(Expr* expr) const -> std::optional<Constant>;
  std::unordered_map<const LocalVariable*, Constant> constants_{};
  auto BuildPattern(const syntax::Node* node) -> PatternExpr*;
  auto BuildLiteralPattern(const syntax::LiteralPattern* pattern) -> PatternExpr*;
  auto BuildArgs(const std::vector<syntax::Node*>& args) -> ExprList;
  auto BuildMember(const syntax::Member* member) -> Expr*;
  auto BuildCallExpr(const syntax::Call* call) -> Expr*;
  auto BuildMatch(const syntax::MatchExpr* match) -> Expr*;
  auto BuildFor(const syntax::ForExpr* loop) -> Expr*;
  auto BuildIf(const syntax::IfExpr* node) -> Expr*;
  auto BuildLambda(const syntax::Lambda* lambda) -> Expr*;
  auto BuildRecord(const syntax::RecordLit* record) -> Expr*;
  auto BuildUi(const syntax::UiElement* element) -> Expr*;
  auto BuildUiChildren(NodeExpr* node, const syntax::UiElement* element) -> void;
  auto BuildBinary(const syntax::Binary* binary) -> Expr*;
  auto LoadName(const syntax::Node* where, const std::string& name) -> Expr*;
  auto ToSeq(Expr* expr) -> SeqExpr*;

  // `let x = ...` / `x = ...`. Redefinition inside the same block reuses the
  // local; redefinition from a nested block shadows it for that block only.
  auto DeclareLocal(const syntax::Span& where, const std::string& name) -> LocalVariable*;
  auto CreateLocal(const syntax::Span& where, const std::string& name) -> LocalVariable*;

  // The text of a node, as it was written.
  auto TextOf(const syntax::Node* node) const -> std::string {
    return std::string(tree_.GetText(node));
  }

  inline auto Literal(Object* rhs) -> Expr* {
    return LiteralExpr::New(rhs);
  }

  auto BuildModule() -> Module*;

 public:
  ExprBuilder(ModuleBuilder* owner, const syntax::SyntaxTree& tree) :
    owner_(owner),
    tree_(tree) {}
  ~ExprBuilder() = default;

  auto GetOwner() const -> ModuleBuilder* {
    return owner_;
  }

  inline auto GetScope() const -> LocalScope* {
    return GetOwner()->GetScope();
  }

  // Builds the whole module. Throws BuildError.
  auto operator()() -> Module* {
    return BuildModule();
  }
};
}  // namespace kura::expr

#endif  // KURA_EXPR_BUILDER_H
