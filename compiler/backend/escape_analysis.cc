#include "backend/escape_analysis.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "backend/compile_type.h"
#include "backend/gir.h"
#include "backend/ir.h"

namespace kura {
namespace {
class Analyzer {
 private:
  FlowGraph* graph_;
  std::unordered_map<Definition*, std::vector<Instruction*>> users_{};
  std::unordered_map<Instruction*, EntryInstr*> blocks_{};
  std::vector<Definition*> candidates_{};
  std::unordered_set<Definition*> is_candidate_{};
  std::unordered_set<Definition*> escaped_{};
  // `holds_[c]` are the allocations stored in `c`: they live as long as it does, so they escape when it does
  std::unordered_map<Definition*, std::vector<Definition*>> holds_{};

  static auto IsCandidate(Instruction* instr) -> bool {
    if (const auto list = instr->AsNewList())
      return true;
    if (const auto record = instr->AsNewRecord())
      return !record->HasInputs();  // a spread makes its size known only when it runs
    if (const auto variant = instr->AsNewVariant())
      return variant->HasPayload() && !variant->IsOpen() && variant->GetVariantName() != "Some";
    if (const auto closure = instr->AsNewClosure())
      return closure->GetTarget() != nullptr;
    return false;
  }

  static auto DefinitionOf(Value* value) -> Definition* {
    return value ? value->GetDefinition() : nullptr;
  }

  // Whether a value of this definition can be an allocation of ours, or hold one: what is read out of a list, a
  // sum or a merge is only worth following if it is something that can. A number can't, and neither can a string,
  // which is an object of its own that holds nothing.
  static auto MayHoldAllocation(Definition* defn) -> bool {
    if (defn == nullptr)
      return false;
    const auto type = defn->GetCompileType();
    if (type == nullptr)
      return true;
    switch (type->GetKind()) {
      case CompileType::kNever:
      case CompileType::kNone:
      case CompileType::kBool:
      case CompileType::kInt:
      case CompileType::kFloat:
      case CompileType::kNumber:
      case CompileType::kString:
#define DEFINE_COLOR_CASE(Name) case CompileType::k##Name:
        FOR_EACH_COLOR_MODEL(DEFINE_COLOR_CASE)
#undef DEFINE_COLOR_CASE
      case CompileType::kVec2:
      case CompileType::kVec3:
      case CompileType::kRect:
      case CompileType::kPx:
      case CompileType::kPct:
      case CompileType::kFr:
      case CompileType::kHandle:
        return false;
      default:
        return true;
    }
  }

  // `result` if it is worth following.
  static auto Follow(Definition* result) -> Definition* {
    return MayHoldAllocation(result) ? result : nullptr;
  }

  void Collect() {
    for (const auto block : graph_->GetReversePostorder()) {
      for (Instruction* instr = block; instr != nullptr; instr = instr->GetNext()) {
        blocks_[instr] = block;
        for (uint64_t idx = 0; idx < instr->GetNumberOfInputs(); idx++) {
          if (const auto defn = DefinitionOf(instr->GetInputAt(idx)))
            users_[defn].push_back(instr);
        }

        if (const auto phi = instr->AsPhi()) {
          for (const auto& in : phi->GetIncoming()) {
            if (const auto defn = DefinitionOf(in.value))
              users_[defn].push_back(instr);
          }
        }

        if (IsCandidate(instr)) {
          const auto defn = static_cast<Definition*>(instr);
          candidates_.push_back(defn);
          is_candidate_.insert(defn);
        }
      }
    }
  }

  // Whether the block can run more than once in one call of the function: its stack block would then have to be
  // as many objects as it runs.
  auto InLoop(EntryInstr* block) const -> bool {
    std::unordered_set<EntryInstr*> seen{};
    std::vector<EntryInstr*> work{};
    for (uint64_t idx = 0; idx < block->GetNumberOfSuccessors(); idx++) {
      if (const auto next = block->GetSuccessorAt(idx))
        work.push_back(next);
    }

    while (!work.empty()) {
      const auto at = work.back();
      work.pop_back();
      if (at == block)
        return true;
      if (!seen.insert(at).second)
        continue;
      for (uint64_t idx = 0; idx < at->GetNumberOfSuccessors(); idx++) {
        if (const auto next = at->GetSuccessorAt(idx))
          work.push_back(next);
      }
    }

    return false;
  }

  void Escape(Definition* defn) {
    if (!escaped_.insert(defn).second)
      return;
    if (const auto it = holds_.find(defn); it != holds_.end()) {
      const auto held = it->second;
      for (const auto other : held)
        Escape(other);
    }
  }

  // `item` is stored in `container`. If that is an allocation that stays on the stack so does the item, and they go
  // together; anything else (a parameter, a value read from somewhere, a call's result) may outlive the function.
  void Contain(Definition* item, Definition* container) {
    if (container == nullptr || !is_candidate_.contains(container)) {
      Escape(item);
      return;
    }
    if (escaped_.contains(container)) {
      Escape(item);
      return;
    }
    holds_[container].push_back(item);
  }

  // Looks at what `user` does with `defn`, which may be `root` or something that was read out of it or merged with it.
  // Returns the definition that now also refers to `root`, if there is one.
  auto Visit(Definition* root, Definition* defn, Instruction* user) -> Definition* {
    const auto result = user->IsDefinition() ? static_cast<Definition*>(user) : nullptr;
    const auto is = [defn](Value* value) {
      return DefinitionOf(value) == defn;
    };

    // what is the same thing, or may be part of it
    if (user->AsPhi() || user->AsLoadVariantPayload())
      return Follow(result);
    if (user->AsBinaryOp() || user->AsUnaryOp())
      return nullptr;
    if (const auto l = user->AsLoadIndex())
      return is(l->GetInstance()) ? Follow(result) : nullptr;
    if (const auto l = user->AsLoadProperty())
      return is(l->GetInstance()) ? Follow(result) : nullptr;
    if (const auto l = user->AsLoadPropertySafely())
      return is(l->GetInstance()) ? Follow(result) : nullptr;
    if (const auto v = user->AsNewVariant()) {
      if (v->GetVariantName() == "Some")
        return Follow(result);  // `Some(x)` is x
      if (v->IsOpen() || !is(v->GetPayload()))
        return nullptr;
      Contain(root, result);
      return nullptr;
    }

    // what keeps it: it lives as long as what it is put in
    if (const auto l = user->AsNewList()) {
      Contain(root, result);
      return nullptr;
    }
    if (const auto c = user->AsNewClosure()) {
      Contain(root, result);
      return nullptr;
    }
    if (const auto s = user->AsStoreProperty()) {
      if (is(s->GetValue()))
        Contain(root, DefinitionOf(s->GetInstance()));
      return nullptr;
    }
    if (const auto s = user->AsStoreIndex()) {
      if (is(s->GetValue()))
        Contain(root, DefinitionOf(s->GetInstance()));
      return nullptr;
    }
    if (const auto s = user->AsSpread()) {
      // what is spread is copied field by field into the record: it stays as long as that does, or what it holds
      // would
      if (is(s->GetValue()))
        Contain(root, DefinitionOf(s->GetInstance()));
      return nullptr;
    }
    if (user->AsNewRecord())
      return nullptr;  // only counted, for its size

    // what only looks at it
    if (const auto c = user->AsCall()) {
      for (uint64_t idx = 0; idx < c->GetNumberOfArgs(); idx++) {
        if (is(c->GetArgAt(idx)))
          Escape(root);
      }
      return nullptr;
    }
    if (user->AsLoadLength() || user->AsTestVariant() || user->AsBranch() || user->AsMeasureText() ||
        user->AsLayoutSetText() || user->AsLayoutSetProperty() || user->AsFillRect() || user->AsPaintBox() ||
        user->AsFillRoundRect() || user->AsFillCircle() || user->AsFillPolygon() || user->AsFillBezier() ||
        user->AsDrawLine() || user->AsDrawRect() || user->AsDrawRoundRect() || user->AsDrawCircle() ||
        user->AsDrawEllipse() || user->AsDrawPolygon() || user->AsDrawText() || user->AsDrawStrokeBezier() ||
        user->AsDrawQuadBezier() || user->AsPushClip() || user->AsLayoutRect() || user->AsRectIntersects() ||
        user->AsCastColor() || user->AsNewColor() || user->AsMathCall() || user->AsAssert() || user->AsAssert())
      return nullptr;
    if (const auto d = user->AsDrawInput()) {
      // the message is kept with the box for the frame; the rest is read now
      if (is(d->GetMessage()))
        Escape(root);
      return nullptr;
    }

    // a return, a registered event (kept for clicks), a store into a variable, and anything not known above
    Escape(root);
    return nullptr;
  }

  void Trace(Definition* root) {
    std::vector<Definition*> work{root};
    std::unordered_set<Definition*> seen{root};
    while (!work.empty()) {
      const auto defn = work.back();
      work.pop_back();
      const auto it = users_.find(defn);
      if (it == users_.end())
        continue;
      for (const auto user : it->second) {
        if (const auto also = Visit(root, defn, user)) {
          if (seen.insert(also).second)
            work.push_back(also);
        }
      }
    }
  }

 public:
  explicit Analyzer(FlowGraph* graph) :
    graph_(graph) {}

  auto Run() -> uint64_t {
    Collect();
    // a block that can run twice can't be a stack block
    for (const auto defn : candidates_) {
      if (InLoop(blocks_[defn]))
        Escape(defn);
    }

    for (const auto defn : candidates_)
      Trace(defn);

    uint64_t kept = 0;
    for (const auto defn : candidates_) {
      if (escaped_.contains(defn))
        continue;
      defn->SetStackAllocated(true);
      kept++;
    }

    return kept;
  }
};
}  // namespace

auto EscapeAnalysis::RunOn(FlowGraph* graph) -> uint64_t {
  return Analyzer(graph).Run();
}

void EscapeAnalysis::Run(ModuleGraph* graphs) {
  for (const auto graph : graphs->GetGraphs()) {
    if (graph == graphs->GetInit())
      continue;
    RunOn(graph);
  }
}
}  // namespace kura
