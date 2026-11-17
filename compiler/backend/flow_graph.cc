#include "backend/flow_graph.h"

#include <algorithm>
#include <unordered_set>

#include "backend/ir.h"
#include "function.h"

namespace kura {
auto FlowGraph::IsExtern() const -> bool {
  return function_ && function_->IsExtern();
}

auto FlowGraph::GetTerminator(EntryInstr* block) -> Instruction* {
  Instruction* last = block;
  while (last->HasNext())
    last = last->GetNext();
  return last;
}

void FlowGraph::DedupeConstants() {
  std::unordered_map<std::string, ConstantInstr*> pool{};
  EntryInstr* entry_block = GetEntry()->GetTarget();
  for (const auto block : GetReversePostorder()) {
    for (Instruction* instr = block->GetNext(); instr != nullptr;) {
      Instruction* next = instr->GetNext();
      if (const auto c = instr->AsConstant()) {
        const std::string key = c->GetCanonicalKey();
        auto [it, inserted] = pool.try_emplace(key, c);
        if (!inserted) {
          ReplaceAllUsesWith(c, it->second);
          Remove(c);
        } else if (block != entry_block) {
          Remove(c);
          Instruction* insert_after = entry_block;
          while (insert_after->GetNext() != nullptr && insert_after->GetNext()->IsParameter())
            insert_after = insert_after->GetNext();

          Instruction* after = insert_after->GetNext();
          Instruction::Link(insert_after, c);
          Instruction::Link(c, after);
        }
      }
      instr = next;
    }
  }
}

void FlowGraph::Finalize() {
  preorder_.clear();
  postorder_.clear();
  reverse_postorder_.clear();

  std::unordered_set<EntryInstr*> visited{};
  struct Frame {
    EntryInstr* block;
    std::vector<EntryInstr*> successors;
    uint64_t next;
  };

  const auto successors_of = [](EntryInstr* block) {
    std::vector<EntryInstr*> result{};
    if (const auto graph = block->AsGraphEntry()) {
      if (graph->HasTarget())
        result.push_back(graph->GetTarget());
      return result;
    }
    const auto last = GetTerminator(block);
    if (const auto branch = last->AsBranch()) {
      result.push_back(branch->GetThen());
      result.push_back(branch->HasElse() ? static_cast<EntryInstr*>(branch->GetElse())
                                         : static_cast<EntryInstr*>(branch->GetJoin()));
    } else if (const auto go = last->AsGoto()) {
      result.push_back(go->GetTarget());
    }
    return result;
  };

  // iterative DFS so deeply nested programs can't overflow the stack
  std::vector<Frame> stack{};
  visited.insert(entry_);
  preorder_.push_back(entry_);
  stack.push_back({entry_, successors_of(entry_), 0});
  while (!stack.empty()) {
    auto& frame = stack.back();
    if (frame.next < frame.successors.size()) {
      const auto succ = frame.successors[frame.next++];
      if (!succ)
        continue;
      frame.block->AddSuccessor(succ);
      succ->AddPredecessor(frame.block);
      if (visited.insert(succ).second) {
        preorder_.push_back(succ);
        stack.push_back({succ, successors_of(succ), 0});
      }
    } else {
      postorder_.push_back(frame.block);
      stack.pop_back();
    }
  }
  reverse_postorder_.assign(postorder_.rbegin(), postorder_.rend());
  ComputeDominators();

  DedupeConstants();
}

// Cooper, Harvey & Kennedy, "A Simple, Fast Dominance Algorithm".
void FlowGraph::ComputeDominators() {
  idom_.clear();
  std::unordered_map<EntryInstr*, uint64_t> order{};  // postorder number
  for (uint64_t idx = 0; idx < postorder_.size(); idx++)
    order[postorder_[idx]] = idx;

  idom_[entry_] = entry_;
  const auto intersect = [&](EntryInstr* a, EntryInstr* b) {
    while (a != b) {
      while (order[a] < order[b])
        a = idom_[a];
      while (order[b] < order[a])
        b = idom_[b];
    }
    return a;
  };

  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto block : reverse_postorder_) {
      if (block == entry_)
        continue;
      EntryInstr* new_idom = nullptr;
      for (uint64_t idx = 0; idx < block->GetNumberOfPredecessors(); idx++) {
        const auto pred = block->GetPredecessorAt(idx);
        if (!idom_.contains(pred))
          continue;
        new_idom = new_idom ? intersect(pred, new_idom) : pred;
      }
      if (new_idom && idom_[block] != new_idom) {
        idom_[block] = new_idom;
        changed = true;
      }
    }
  }
}

auto FlowGraph::GetImmediateDominator(EntryInstr* block) const -> EntryInstr* {
  if (block == entry_)
    return nullptr;
  const auto it = idom_.find(block);
  return it == idom_.end() ? nullptr : it->second;
}

auto FlowGraph::Dominates(EntryInstr* a, EntryInstr* b) const -> bool {
  for (auto block = b; block != nullptr; block = GetImmediateDominator(block)) {
    if (block == a)
      return true;
  }
  return false;
}

auto FlowGraph::Verify(std::string* error) const -> bool {
  const auto fail = [&](const std::string& message) {
    if (error)
      *error = name_ + ": " + message;
    return false;
  };

  // where each instruction lives
  std::unordered_map<Instruction*, std::pair<EntryInstr*, uint64_t>> where{};
  for (const auto block : preorder_) {
    uint64_t position = 0;
    for (Instruction* instr = block; instr != nullptr; instr = instr->GetNext())
      where[instr] = {block, position++};
  }

  for (const auto block : preorder_) {
    if (block->IsGraphEntry())
      continue;
    const auto last = GetTerminator(block);
    if (!(last->IsBranch() || last->IsGoto() || last->IsReturn()))
      return fail("B" + std::to_string(block->GetBlockId()) + " does not end in a terminator");
    for (Instruction* instr = block->GetNext(); instr != last; instr = instr->GetNext()) {
      if (instr->IsBranch() || instr->IsGoto() || instr->IsReturn())
        return fail("B" + std::to_string(block->GetBlockId()) + " has a terminator before its end");
    }
  }

  const auto dominates_use = [&](Definition* def, EntryInstr* use_block, uint64_t use_position,
                                 const bool at_end) -> bool {
    const auto found = where.find(def);
    if (found == where.end())
      return false;
    const auto& [def_block, def_position] = found->second;
    if (def_block == use_block)
      return at_end || def_position < use_position;
    return Dominates(def_block, use_block);
  };

  for (const auto block : preorder_) {
    if (const auto join = block->AsJoinEntry()) {
      for (const auto phi : join->GetPhis()) {
        if (phi->GetNumberOfIncoming() != block->GetNumberOfPredecessors())
          return fail("phi %" + std::to_string(phi->GetId()) + " has " + std::to_string(phi->GetNumberOfIncoming()) +
                      " incoming values for " + std::to_string(block->GetNumberOfPredecessors()) + " predecessors");
        for (const auto& in : phi->GetIncoming()) {
          bool is_pred = false;
          for (uint64_t idx = 0; idx < block->GetNumberOfPredecessors(); idx++)
            is_pred |= block->GetPredecessorAt(idx) == in.predecessor;
          if (!is_pred)
            return fail("phi %" + std::to_string(phi->GetId()) + " has an incoming value from B" +
                        std::to_string(in.predecessor->GetBlockId()) + ", which is not a predecessor");
          if (!in.value || !in.value->GetDefinition())
            return fail("phi %" + std::to_string(phi->GetId()) + " has a missing incoming value");
          if (!dominates_use(in.value->GetDefinition(), in.predecessor, 0, true))
            return fail("phi %" + std::to_string(phi->GetId()) + " uses %" +
                        std::to_string(in.value->GetDefinition()->GetId()) + " which does not dominate B" +
                        std::to_string(in.predecessor->GetBlockId()));
        }
      }
    }

    for (Instruction* instr = block; instr != nullptr; instr = instr->GetNext()) {
      if (instr->IsPhi())
        continue;
      for (uint64_t idx = 0; idx < instr->GetNumberOfInputs(); idx++) {
        const auto input = instr->GetInputAt(idx);
        if (!input) {
          // optional inputs (a variant's payload, a branch without else) are fine
          if (instr->IsNewVariant())
            continue;
          return fail("%" + std::to_string(instr->GetId()) + " has a missing input " + std::to_string(idx));
        }
        if (!input->GetDefinition() || !dominates_use(input->GetDefinition(), block, where[instr].second, false))
          return fail("%" + std::to_string(instr->GetId()) + " uses a value that does not dominate it");
      }
    }
  }
  return true;
}

void FlowGraph::Remove(Instruction* instr) {
  Instruction* prev = instr->GetPrevious();
  Instruction* next = instr->GetNext();
  if (prev)
    prev->SetNext(next);
  if (next)
    next->SetPrevious(prev);
  instr->SetNext(nullptr);
  instr->SetPrevious(nullptr);
}

void FlowGraph::ReplaceAllUsesWith(Definition* from, Definition* to) {
  for (const auto block : GetReversePostorder()) {
    for (Instruction* instr = block; instr != nullptr; instr = instr->GetNext()) {
      for (uint64_t i = 0; i < instr->GetNumberOfInputs(); i++) {
        if (const auto input = instr->GetInputAt(i); input && input->GetDefinition() == from) {
          instr->SetInputAt(i, Value::New(to));  // TODO(@s0cks): probably need to free the old input
        }
      }

      if (auto phi = instr->AsPhi()) {
        for (auto& in : phi->GetIncoming()) {
          if (in.value && in.value->GetDefinition() == from) {
            in.value = Value::New(to);
          }
        }
      }
    }
  }
}
}  // namespace kura
