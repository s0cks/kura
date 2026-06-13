#ifndef KURA_MODULE_H
#define KURA_MODULE_H

#include <functional>
#include <string>

#include "frontend/expr.h"
#include "function.h"
#include "view.h"

namespace kura {
namespace expr {
class ExprBuilder;
}

struct Module {
  friend class expr::ExprBuilder;
  std::string name{};
  std::vector<Function*> functions{};
  View* view = nullptr;

  Module() = delete;
  Module(const std::string n) :
    name(std::move(n)) {}
  ~Module() = delete;

  inline auto HasView() const -> bool {
    return view != nullptr;
  }

  auto ToString() const -> std::string;
  auto VisitFunctions(const std::function<bool(Function*)> vis) -> bool;

 private:
  inline void AddFunction(Function* func) {
    functions.push_back(func);
  }

  inline void AddFunctions(const std::vector<Function*>& funcs) {
    functions.insert(std::end(functions), std::begin(funcs), std::end(funcs));
  }

 public:
  static inline auto New(const std::string name) -> Module* {
    return new Module(std::move(name));
  }
};
}  // namespace kura

#endif  // KURA_MODULE_H
