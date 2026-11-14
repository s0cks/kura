#ifndef KURA_DOM_BUILDER_H
#define KURA_DOM_BUILDER_H

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "dom/node.h"
#include "frontend/expr.h"

namespace kura {
class Function;
class LocalVariable;
class Object;

namespace dom {
class Builder {
  DEFINE_NON_COPYABLE_TYPE(Builder);

 public:
  using ConstantResolver = std::function<Object*(LocalVariable*)>;
  // The function a global local names, or null if it names none.
  using FunctionResolver = std::function<Function*(LocalVariable*)>;

 private:
  Tree* tree_;
  ConstantResolver resolver_;
  FunctionResolver functions_{};
  // the helpers being inlined right now, innermost last: a helper that reaches itself is recursion
  std::vector<Function*> inlining_{};
  std::vector<std::string> errors_{};
  // the `let`s seen so far that hold a constant
  std::unordered_map<LocalVariable*, Object*> constants_{};
  // the locals this tree binds: what the resolver knows of them is from some other tree, so it is not used
  std::unordered_set<LocalVariable*> bound_{};

  void Error(const std::string& message);

  auto Fold(expr::Expr* expr) -> Object*;
  void BuildInto(Node* parent, expr::Expr* expr, const TagList& tags);
  void BuildElement(Node* parent, expr::NodeExpr* expr, const TagList& tags);
  void BuildProperties(Node* node, expr::NodeExpr* expr);
  void BuildBind(Node* parent, expr::StoreLocalExpr* expr);
  auto FindHelper(expr::CallExpr* call) -> Function*;
  void InlineHelper(Node* parent, Function* helper, expr::CallExpr* call, const TagList& tags);
  void ReportUnknownProperty(Node* node, const std::string& name);

 public:
  explicit Builder(Tree* tree, ConstantResolver resolver = {}) :
    tree_(tree),
    resolver_(std::move(resolver)) {}
  ~Builder() = default;

  // Lets a call to a function that returns a Node be built into the tree as the nodes the function
  // returns, so a helper takes part in its caller's layout.
  inline void SetFunctionResolver(FunctionResolver rhs) {
    functions_ = std::move(rhs);
  }

  // The value of an expression made of constants, or null if it isn't one.
  inline auto FoldConstant(expr::Expr* expr) -> Object* {
    return Fold(expr);
  }

  // Whether a call is to a helper that is built into the tree (see SetFunctionResolver).
  inline auto IsHelperCall(expr::CallExpr* call) -> bool {
    return FindHelper(call) != nullptr;
  }

  auto Build(expr::Expr* expr) -> Node*;

  inline auto Ok() const -> bool {
    return errors_.empty();
  }

  inline auto GetErrors() const -> const std::vector<std::string>& {
    return errors_;
  }
};
}  // namespace dom
}  // namespace kura

#endif  // KURA_DOM_BUILDER_H
