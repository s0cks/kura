#ifndef KURA_DOM_BUILDER_H
#define KURA_DOM_BUILDER_H

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "dom/node.h"
#include "frontend/expr.h"

namespace kura {
class LocalVariable;
class Object;

namespace dom {
class Builder {
  DEFINE_NON_COPYABLE_TYPE(Builder);

 public:
  using ConstantResolver = std::function<Object*(LocalVariable*)>;

 private:
  Tree* tree_;
  ConstantResolver resolver_;
  std::vector<std::string> errors_{};
  // the `let`s seen so far that hold a constant
  std::unordered_map<LocalVariable*, Object*> constants_{};

  void Error(const std::string& message);

  auto Fold(expr::Expr* expr) -> Object*;
  void BuildInto(Node* parent, expr::Expr* expr, const TagList& tags);
  void BuildElement(Node* parent, expr::NodeExpr* expr, const TagList& tags);
  void BuildProperties(Node* node, expr::NodeExpr* expr);
  void BuildBind(Node* parent, expr::StoreLocalExpr* expr);
  void ReportUnknownProperty(Node* node, const std::string& name);

 public:
  explicit Builder(Tree* tree, ConstantResolver resolver = {}) :
    tree_(tree),
    resolver_(std::move(resolver)) {}
  ~Builder() = default;

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
