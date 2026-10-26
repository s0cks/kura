#include "backend/purity_analysis.h"

#include <map>
#include <optional>
#include <set>

#include "backend/compile_type.h"
#include "backend/ir.h"
#include "c_type.h"
#include "frontend/type_expr.h"
#include "function.h"
#include "module.h"

namespace kura {
namespace {
using Path = std::vector<std::string>;

class Walker {
 private:
  ModuleGraph* graphs_;
  std::map<FlowGraph*, std::optional<Path>> memo_{};
  std::set<FlowGraph*> active_{};

  static auto NameOf(FlowGraph* graph) -> std::string {
    return graph->GetFunction() ? std::string(graph->GetFunction()->GetName()->GetData()) : "a lambda";
  }

  // The path from `graph` to an impure C function, if there is one.
  auto Search(FlowGraph* graph) -> std::optional<Path> {
    if (const auto it = memo_.find(graph); it != memo_.end())
      return it->second;
    if (!active_.insert(graph).second)
      return std::nullopt;  // recursion is rejected elsewhere
    std::optional<Path> found{};
    for (const auto block : graph->GetReversePostorder()) {
      for (auto instr = block->GetNext(); instr && !found; instr = instr->GetNext()) {
        if (const auto load = instr->AsLoadFunction()) {
          const auto function = load->GetFunction();
          if (function->IsExtern()) {
            if (!function->GetCSignature()->pure)
              found = Path{NameOf(graph), std::string(function->GetName()->GetData())};
          } else if (const auto callee = graphs_->GetGraphFor(function)) {
            if (auto inner = Search(callee)) {
              inner->insert(inner->begin(), NameOf(graph));
              found = std::move(inner);
            }
          }
        } else if (const auto closure = instr->AsNewClosure()) {
          if (auto inner = Search(closure->GetTarget())) {
            inner->insert(inner->begin(), NameOf(graph));
            found = std::move(inner);
          }
        }
      }
      if (found)
        break;
    }
    active_.erase(graph);
    memo_[graph] = found;
    return found;
  }

 public:
  explicit Walker(ModuleGraph* graphs) :
    graphs_(graphs) {}

  auto Impurity(FlowGraph* graph) -> std::optional<Path> {
    return Search(graph);
  }
};

auto Chain(const Path& path) -> std::string {
  std::string text{};
  for (size_t idx = 0; idx < path.size(); idx++)
    text += (idx ? " -> " : "") + path[idx];
  return text;
}

auto HasDirective(Function* function, const std::string& name) -> bool {
  for (const auto directive : function->GetDirectives()) {
    if (directive->name == name)
      return true;
  }
  return false;
}
}  // namespace

auto PurityAnalysis::Run(Module* module, ModuleGraph* graphs, std::vector<std::string>* errors) -> bool {
  Walker walker(graphs);
  bool ok = true;
  const auto fail = [&](const std::string& message) {
    errors->push_back(message);
    ok = false;
  };

  FlowGraph* view = nullptr;
  FlowGraph* update = nullptr;
  FlowGraph* init = nullptr;
  for (const auto function : module->functions) {
    const auto graph = graphs->GetGraphFor(function);
    if (!graph || function->IsExtern())
      continue;
    const std::string name(function->GetName()->GetData());
    if (HasDirective(function, "pure")) {
      if (const auto path = walker.Impurity(graph))
        fail("@pure function '" + name + "' is not pure: " + Chain(*path) +
             " (a C function is pure when its header says `const` or `pure`, or it is declared `@pure`)");
    }
    if (name == "view")
      view = graph;
    else if (name == "update")
      update = graph;
    else if (name == "init")
      init = graph;
  }

  if (view) {
    if (const auto path = walker.Impurity(view))
      fail("view is not pure: " + Chain(*path) +
           ". view must be a pure function of the model; call C from init or update, or declare the C function"
           " `@pure` if it has no effects");
  }
  if (const auto top = graphs->GetInit()) {
    if (const auto path = walker.Impurity(top))
      fail("a top-level binding is not pure: " + Chain(*path) +
           ". Top-level bindings run when the module loads; only pure C functions may be called there");
  }

  // The state is what init and update produce, and what view reads.
  if (update && update->GetNumberOfParameters() == 2 && update->GetParameterTypes().size() == 2 &&
      update->GetReturnType()) {
    const auto model = update->GetParameterTypes()[1];
    if (!CompileType::IsAssignable(update->GetReturnType(), model))
      fail("update must produce the model: it returns " + update->GetReturnType()->ToString() +
           " but its model parameter is " + model->ToString());
    if (init && init->GetReturnType() && !CompileType::IsAssignable(init->GetReturnType(), model))
      fail("init must produce the model: it returns " + init->GetReturnType()->ToString() +
           " but update's model parameter is " + model->ToString());
  }

  if (view && view->GetReturnType() && !CompileType::IsAssignable(view->GetReturnType(), CompileType::Node()))
    fail("view must produce a Node: it returns " + view->GetReturnType()->ToString());

  return ok;
}
}  // namespace kura
