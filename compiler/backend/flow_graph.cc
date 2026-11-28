#include "backend/flow_graph.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

#include "backend/ir.h"
#include "color.h"
#include "function.h"
#include "gir.h"
#include "heap/color_model.h"
#include "ir.h"
#include "type_id.h"

namespace kura {
static inline auto Name(const Value* value) -> std::string {
  if (!value || !value->GetDefinition())
    return "<none>";

  return "%" + std::to_string(value->GetDefinition()->GetId());
}

static inline auto IsEmptyJumpBlock(EntryInstr* block, EntryInstr* target) -> bool {
  if (!block || block->IsJoinEntry())
    return false;
  // A block is an empty jump if its very first instruction after EntryInstr is Goto(target)
  Instruction* first = block->GetNext();
  if (!first || !first->IsGoto())
    return false;
  return first->AsGoto()->GetTarget() == target;
}

static inline auto HasNoSideEffects(Instruction* instr) -> bool {
  if (!instr->IsDefinition())
    return false;

  // Never delete parameters, phis, or stateful layout/render operations
  if (instr->IsParameter() || instr->IsPhi())
    return false;

  return instr->IsConstant() || instr->IsBinaryOp() || instr->IsUnaryOp() || instr->IsLoadProperty() ||
         instr->IsLoadIndex() || instr->IsLoadLength() || instr->IsLoadVariantPayload() || instr->IsTestVariant() ||
         instr->IsNewVariant() || instr->IsNewList() || instr->IsNewRecord() || instr->IsLayoutRect() ||
         instr->IsRectIntersects() || instr->IsRectPastClip() || instr->IsNewColor() || instr->IsCastColor() ||
         instr->IsMathCall() || instr->IsLoadFunction() || instr->IsCast() || instr->IsNewTuple() ||
         instr->IsSliceTuple();
}

static inline auto IsZeroAlphaConstant(Definition* def) -> bool {
  const auto c = def ? def->AsConstant() : nullptr;
  if (!c)
    return false;

  Object* val = c->GetValue();
  if (!val)
    return true;

  if (val->GetType() == BuiltinTypes::kIntType) {
    const int64_t rgba = static_cast<Int*>(val)->GetValue();
    return rgba == 0 || (rgba & 0xFF) == 0;
  }

#define DEFINE_CHECK(Name)                           \
  if (val->GetType() == BuiltinTypes::k##Name##Type) \
    return static_cast<Name*>(val)->IsTransparent();

  FOR_EACH_COLOR_MODEL(DEFINE_CHECK)
#undef DEFINE_CHECK

  return false;
}

static inline auto IsZeroFloatConstant(Definition* def) -> bool {
  const auto c = def ? def->AsConstant() : nullptr;
  if (!c)
    return false;
  Object* val = c->GetValue();
  if (!val)
    return false;

  if (val->GetType() == BuiltinTypes::kFloatType)
    return static_cast<Float*>(val)->GetValue() <= 0.0f;

  if (val->GetType() == BuiltinTypes::kIntType)
    return static_cast<Int*>(val)->GetValue() <= 0;

  return false;
}

static inline auto StringifyConstantObject(Object* val) -> std::optional<std::string> {
  if (!val)
    return std::nullopt;
  if (val->GetType() == BuiltinTypes::kStringType)
    return static_cast<String*>(val)->GetValue();

  if (val->GetType() == BuiltinTypes::kIntType)
    return std::to_string(static_cast<Int*>(val)->GetValue());

  if (val->GetType() == BuiltinTypes::kBoolType)
    return static_cast<Bool*>(val)->GetValue() ? "true" : "false";

  if (val->GetType() == BuiltinTypes::kFloatType)
    return std::to_string(static_cast<Float*>(val)->GetValue());

  return std::nullopt;
}

static inline auto AsStringConstant(Definition* def) -> String* {
  const auto c = def ? def->AsConstant() : nullptr;
  if (!c || !c->GetValue() || c->GetValue()->GetType() != BuiltinTypes::kStringType)
    return nullptr;

  return static_cast<String*>(c->GetValue());
}

auto FlowGraph::FoldConstantStrings() -> bool {
  bool any_changed = false;
  bool changed = true;

  while (changed) {
    changed = false;

    for (const auto block : reverse_postorder_) {
      for (Instruction* instr = block->GetNext(); instr != nullptr;) {
        Instruction* next = instr->GetNext();

        // 1. Fold UnaryOp(Stringify, Constant) -> Constant(String)
        if (const auto unary = instr->AsUnaryOp(); unary && unary->GetOp() == UnaryOp::kStringify) {
          Definition* operand = unary->GetInputAt(0)->GetDefinition();
          if (const auto c = operand ? operand->AsConstant() : nullptr) {
            if (auto str = StringifyConstantObject(c->GetValue())) {
              auto* new_const = ConstantInstr::New(String::New(*str));
              ReplaceAllUsesWith(unary, new_const);
              Replace(unary, new_const);  // <--- Use Replace(unary, new_const) instead of Link + Remove!
              changed = true;
              any_changed = true;
            }
          }
        }
        // 2. Fold BinaryOp(Add, ...) on Strings
        else if (const auto bin = instr->AsBinaryOp(); bin && bin->GetOp() == BinaryOp::kAdd) {
          Definition* lhs = bin->GetInputAt(0)->GetDefinition();
          Definition* rhs = bin->GetInputAt(1)->GetDefinition();
          String* lhs_str = AsStringConstant(lhs);
          String* rhs_str = AsStringConstant(rhs);

          // Case A: Constant(String) + Constant(String) -> Constant(String)
          if (lhs_str && rhs_str) {
            auto* merged = ConstantInstr::New(String::New(lhs_str->GetValue() + rhs_str->GetValue()));
            ReplaceAllUsesWith(bin, merged);
            Replace(bin, merged);  // <--- Remove the two Instruction::Link lines right above this!
            changed = true;
            any_changed = true;
          }
          // Case B: Identity elimination ("" + X -> X, or X + "" -> X when X is known String)
          else if (lhs_str && lhs_str->GetValue().empty() && rhs) {
            ReplaceAllUsesWith(bin, rhs);
            Remove(bin);
            changed = true;
            any_changed = true;
          } else if (rhs_str && rhs_str->GetValue().empty() && lhs) {
            ReplaceAllUsesWith(bin, lhs);
            Remove(bin);
            changed = true;
            any_changed = true;
          }
          // Case C: Re-associate ((X + C1) + C2) -> (X + (C1 + C2)) when C1 and C2 are String constants
          else if (rhs_str) {
            if (const auto lhs_bin = lhs ? lhs->AsBinaryOp() : nullptr; lhs_bin && lhs_bin->GetOp() == BinaryOp::kAdd) {
              Definition* inner_lhs = lhs_bin->GetInputAt(0)->GetDefinition();
              Definition* inner_rhs = lhs_bin->GetInputAt(1)->GetDefinition();
              if (String* c1 = AsStringConstant(inner_rhs)) {
                auto* merged_c = ConstantInstr::New(String::New(c1->GetValue() + rhs_str->GetValue()));
                auto* new_add = BinaryOpInstr::New(BinaryOp::kAdd, Value::New(inner_lhs), Value::New(merged_c));

                Instruction* prev = bin->GetPrevious();
                ReplaceAllUsesWith(bin, new_add);
                Remove(bin);  // Unlink `bin` FIRST!
                Instruction::Link(prev, merged_c);
                Instruction::Link(merged_c, new_add);
                Instruction::Link(new_add, next);
                changed = true;
                any_changed = true;
              }
            }
          }
        }

        instr = next;
      }
    }

    // Re-dedupe & hoist newly created ConstantInstrs into B0, then sweep dead inner BinaryOps
    if (changed) {
      DedupeConstants();
      EliminateDeadCode();
    }
  }

  return any_changed;
}

static inline auto GetCSEKey(Instruction* instr) -> std::optional<std::string> {
  if (!instr->IsDefinition())
    return std::nullopt;

  // Format input definition IDs cleanly
  const auto inputs_key = [&]() {
    std::string s = "(";
    for (uint64_t i = 0; i < instr->GetNumberOfInputs(); i++) {
      if (i > 0)
        s += ",";
      const auto in = instr->GetInputAt(i);
      s += in && in->GetDefinition() ? std::to_string(in->GetDefinition()->GetId()) : "null";
    }
    s += ")";
    return s;
  };

  if (const auto prop = instr->AsLoadProperty())
    return "LoadProperty:" + std::string{prop->GetProperty()->GetPropertyName()->GetData()} + inputs_key();

  if (instr->IsLoadLength())
    return "LoadLength" + inputs_key();

  if (instr->IsLoadIndex())
    return "LoadIndex" + inputs_key();

  if (const auto mc = instr->AsMathCall())
    return "MathCall:" + std::to_string(static_cast<int>(mc->GetOp())) + inputs_key();

  if (const auto bin = instr->AsBinaryOp())
    return "BinaryOp:" + std::to_string(static_cast<int>(bin->GetOp())) + inputs_key();

  if (const auto un = instr->AsUnaryOp())
    return "UnaryOp:" + std::to_string(static_cast<int>(un->GetOp())) + inputs_key();

  if (const auto rect = instr->AsLayoutRect()) {
    if (rect->GetPlan()->GetIndex() == 0)
      return "LayoutRect:0:" + std::to_string(rect->GetNode());

    return std::nullopt;
  }

  if (const auto c = instr->AsCast())
    return std::format("Cast:{}:{}:{}", static_cast<int>(c->GetToType()), static_cast<int>(c->GetFromType()),
                       Name(c->GetValue()));

  if (const auto nc = instr->AsNewColor())
    return "NewColor:" + std::to_string(static_cast<int>(nc->GetModel())) + inputs_key();

  if (const auto cc = instr->AsCastColor())
    return "CastColor:" + std::to_string(static_cast<int>(cc->GetTarget())) + inputs_key();

  return std::nullopt;
}

auto FlowGraph::EliminateCommonSubexpressions() -> bool {
  bool changed = false;

  // 1. Build dominator tree children from idom_
  std::unordered_map<EntryInstr*, std::vector<EntryInstr*>> dom_children{};
  for (const auto block : preorder_) {
    if (EntryInstr* parent = GetImmediateDominator(block)) {
      dom_children[parent].push_back(block);
    }
  }

  // 2. Scoped expression table: key -> dominating Definition*
  std::unordered_map<std::string, Definition*> available{};

  const auto visit = [&](auto& self, EntryInstr* block) -> void {
    std::vector<std::string> added_keys{};

    for (Instruction* instr = block->GetNext(); instr != nullptr;) {
      Instruction* next = instr->GetNext();

      if (auto key = GetCSEKey(instr)) {
        auto [it, inserted] = available.try_emplace(*key, dynamic_cast<Definition*>(instr));
        if (!inserted) {
          // Dominated duplicate found -> replace all uses and remove!
          ReplaceAllUsesWith(dynamic_cast<Definition*>(instr), it->second);
          Remove(instr);
          changed = true;
        } else {
          added_keys.push_back(std::move(*key));
        }
      }

      instr = next;
    }

    // Recurse into blocks dominated by `block`
    for (EntryInstr* child : dom_children[block]) {
      self(self, child);
    }

    // Pop expressions defined in this block when leaving its dominance scope
    for (const auto& key : added_keys) {
      available.erase(key);
    }
  };

  visit(visit, entry_);
  return changed;
}

void FlowGraph::Replace(Instruction* old_instr, Instruction* new_instr) {
  Instruction* prev = old_instr->GetPrevious();
  Instruction* next = old_instr->GetNext();
  Remove(old_instr);
  if (prev)
    Instruction::Link(prev, new_instr);
  if (new_instr)
    Instruction::Link(new_instr, next);
}

auto FlowGraph::EliminateEmptyLoops() -> bool {
  bool changed = false;

  for (const auto block : reverse_postorder_) {
    const auto header = block->AsJoinEntry();
    if (!header)
      continue;

    // 1. Find back-edge(s): predecessors dominated by `header`
    std::vector<EntryInstr*> back_edges{};
    for (uint64_t i = 0; i < header->GetNumberOfPredecessors(); i++) {
      EntryInstr* pred = header->GetPredecessorAt(i);
      if (Dominates(header, pred)) {
        back_edges.push_back(pred);
      }
    }
    if (back_edges.empty())
      continue;

    // 2. Header must end in a conditional Branch(cond, then=body, else=exit)
    const auto branch = GetTerminator(header)->AsBranch();
    if (!branch || !branch->HasElse())
      continue;

    EntryInstr* then_block = branch->GetThen();
    EntryInstr* else_block = branch->GetElse();

    // Collect all blocks in the natural loop via reverse DFS from back_edges to header
    std::unordered_set<EntryInstr*> loop_blocks{header};
    std::vector<EntryInstr*> worklist = back_edges;
    while (!worklist.empty()) {
      EntryInstr* curr = worklist.back();
      worklist.pop_back();
      if (loop_blocks.insert(curr).second) {
        for (uint64_t i = 0; i < curr->GetNumberOfPredecessors(); i++) {
          worklist.push_back(curr->GetPredecessorAt(i));
        }
      }
    }

    // Determine which branch target stays inside the loop vs. exits it
    EntryInstr* exit_block = nullptr;
    if (loop_blocks.contains(then_block) && !loop_blocks.contains(else_block)) {
      exit_block = else_block;
    } else if (loop_blocks.contains(else_block) && !loop_blocks.contains(then_block)) {
      exit_block = then_block;
    } else {
      continue;  // Complex multi-exit loop; skip
    }

    // 3. Verify no values defined inside the loop are used outside the loop,
    //    and no instructions inside the loop have side effects.
    bool is_empty = true;
    for (EntryInstr* lb : loop_blocks) {
      // Check Phis on blocks other than header
      if (lb != header) {
        if (const auto join = lb->AsJoinEntry(); join && !join->GetPhis().empty()) {
          is_empty = false;
          break;
        }
      }

      for (Instruction* instr = lb->GetNext(); instr != nullptr; instr = instr->GetNext()) {
        if (instr->IsPhi() || instr->IsGoto() || instr->IsBranch())
          continue;

        // Allow pure ops and LayoutSlotInstance (cursor advance only)
        if (!HasNoSideEffects(instr) && !instr->IsLayoutSlotInstance()) {
          is_empty = false;
          break;
        }
      }
      if (!is_empty)
        break;
    }

    if (!is_empty)
      continue;

    // 4. Verify no definition inside `loop_blocks` escapes to a block outside `loop_blocks`
    for (const auto outside : reverse_postorder_) {
      if (loop_blocks.contains(outside))
        continue;
      for (Instruction* instr = outside->GetNext(); instr != nullptr; instr = instr->GetNext()) {
        for (uint64_t i = 0; i < instr->GetNumberOfInputs(); i++) {
          if (const auto in = instr->GetInputAt(i); in && in->GetDefinition()) {
            // Check if definition lives in loop_blocks
            for (EntryInstr* lb : loop_blocks) {
              for (Instruction* li = lb; li != nullptr; li = li->GetNext()) {
                if (li == in->GetDefinition()) {
                  is_empty = false;
                  break;
                }
              }
            }
          }
        }
      }
    }

    if (!is_empty)
      continue;

    // 5. Bypass the loop: replace header's Branch with Goto(exit_block)
    Instruction* prev = branch->GetPrevious();
    Remove(branch);
    Instruction::Link(prev, GotoInstr::New(exit_block));
    changed = true;
  }

  return changed;
}

auto FlowGraph::EliminateRedundantClips() -> bool {
  bool changed = false;
  std::unordered_set<Definition*> dead_clips{};

  for (const auto block : reverse_postorder_) {
    struct ClipFrame {
      PushClipInstr* push = nullptr;
      Definition* rect = nullptr;
      bool redundant = false;
      bool drew_anything = false;
    };
    std::vector<ClipFrame> stack{};

    for (Instruction* instr = block->GetNext(); instr != nullptr;) {
      Instruction* next = instr->GetNext();

      if (const auto push = instr->AsPushClip()) {
        Definition* rect = push->GetRect()->GetDefinition();
        const bool same_as_current = !stack.empty() && stack.back().rect == rect;

        if (!stack.empty())
          stack.back().drew_anything = true;

        stack.push_back(ClipFrame{push, rect, same_as_current, false});
        if (same_as_current) {
          dead_clips.insert(push);
          Remove(push);
          changed = true;
        }
      } else if (const auto pop = instr->AsPopClip()) {
        if (!stack.empty()) {
          ClipFrame top = stack.back();
          stack.pop_back();

          if (top.redundant) {
            dead_clips.insert(pop);
            Remove(pop);
            changed = true;
          } else if (!top.drew_anything) {
            dead_clips.insert(top.push);
            dead_clips.insert(pop);
            Remove(top.push);
            Remove(pop);
            changed = true;
          }
        }
      } else if (const auto check = instr->AsCheckRendererStatus()) {
        Definition* target = check->GetValue() ? check->GetValue()->GetDefinition() : nullptr;
        if (target && dead_clips.contains(target)) {
          Remove(check);
          changed = true;
        }
      } else if (instr->IsGraphicsInstr() && !instr->IsLayoutRect()) {
        if (!stack.empty())
          stack.back().drew_anything = true;
      }

      instr = next;
    }
  }

  return changed;
}

auto FlowGraph::SimplifyCFG() -> bool {
  bool changed = false;

  // 1. Collapse trivial Phis (1 incoming value, or all incoming values identical)
  for (const auto block : reverse_postorder_) {
    const auto join = block->AsJoinEntry();
    if (!join)
      continue;

    auto& phis = join->GetPhis();  // If GetPhis() is const, add a mutable GetPhis() or RemovePhi() on JoinEntryInstr
    std::erase_if(phis, [&](PhiInstr* phi) {
      if (phi->GetNumberOfIncoming() == 0)
        return false;

      Definition* same_def = phi->GetIncomingAt(0).value ? phi->GetIncomingAt(0).value->GetDefinition() : nullptr;
      if (!same_def || same_def == phi)
        return false;

      for (uint64_t i = 1; i < phi->GetNumberOfIncoming(); i++) {
        Definition* d = phi->GetIncomingAt(i).value ? phi->GetIncomingAt(i).value->GetDefinition() : nullptr;
        if (d != same_def)
          return false;
      }

      ReplaceAllUsesWith(phi, same_def);
      changed = true;
      return true;
    });
  }

  // 2. Merge linear Goto chains: if A ends in Goto(B) and B has only 1 predecessor (A) and no Phis,
  //    splice B's instructions onto A and remove the intermediate Goto.
  for (const auto block : reverse_postorder_) {
    if (block->IsGraphEntry())
      continue;

    Instruction* term = GetTerminator(block);
    const auto go = term ? term->AsGoto() : nullptr;
    if (!go)
      continue;

    EntryInstr* target = go->GetTarget();
    if (!target || target == entry_ || target->GetNumberOfPredecessors() != 1)
      continue;

    if (const auto join = target->AsJoinEntry(); join && !join->GetPhis().empty())
      continue;

    // Splice target's instruction chain onto block right before `go`, then remove `go`
    Instruction* prev = go->GetPrevious();
    Instruction* first_in_target = target->GetNext();
    Remove(go);

    if (first_in_target) {
      Instruction::Link(prev, first_in_target);
      target->SetNext(nullptr);
    }

    // Update any Phi incoming references that pointed to `target` so they now point to `block`
    for (const auto succ_block : reverse_postorder_) {
      if (const auto join = succ_block->AsJoinEntry()) {
        for (const auto phi : join->GetPhis()) {
          for (auto& in : phi->GetIncoming()) {
            if (in.predecessor == target)
              in.predecessor = block;
          }
        }
      }
    }

    changed = true;
    break;
  }

  const auto can_thread = [&](EntryInstr* from, EntryInstr* via) -> EntryInstr* {
    if (!via || via == entry_ || via->IsJoinEntry())
      return nullptr;
    Instruction* first = via->GetNext();
    if (!first || !first->IsGoto())
      return nullptr;
    EntryInstr* dest = first->AsGoto()->GetTarget();
    if (!dest || dest == via)
      return nullptr;

    if (const auto join = dest->AsJoinEntry(); join && !join->GetPhis().empty()) {
      if (via->GetNumberOfPredecessors() != 1)
        return nullptr;
      for (uint64_t i = 0; i < dest->GetNumberOfPredecessors(); i++) {
        if (dest->GetPredecessorAt(i) == from)
          return nullptr;
      }
    }
    return dest;
  };

  const auto rewrite_phi_pred = [](EntryInstr* dest, EntryInstr* old_pred, EntryInstr* new_pred) {
    if (const auto join = dest->AsJoinEntry()) {
      for (const auto phi : join->GetPhis()) {
        for (auto& in : phi->GetIncoming()) {
          if (in.predecessor == old_pred)
            in.predecessor = new_pred;
        }
      }
    }
  };

  for (const auto block : reverse_postorder_) {
    if (block->IsGraphEntry())
      continue;
    const auto branch = GetTerminator(block)->AsBranch();
    if (!branch)
      continue;

    EntryInstr* old_then = branch->GetThen();
    if (EntryInstr* dest = can_thread(block, old_then)) {
      if (auto* target = dest->AsTargetEntry()) {
        rewrite_phi_pred(dest, old_then, block);
        branch->SetThen(target);
        changed = true;
      }
    }

    if (branch->HasElse()) {
      EntryInstr* old_else = branch->GetElse();
      if (EntryInstr* dest = can_thread(block, old_else)) {
        if (auto* target = dest->AsTargetEntry()) {
          rewrite_phi_pred(dest, old_else, block);
          branch->SetElse(target);
          changed = true;
        }
      }
    } else {
      EntryInstr* old_join = branch->GetJoin();
      if (EntryInstr* dest = can_thread(block, old_join)) {
        if (auto* join = dest->AsJoinEntry()) {
          rewrite_phi_pred(dest, old_join, block);
          branch->SetJoin(join);
          changed = true;
        }
      }
    }
  }

  return changed;
}

// Helper to insert `new_instr` immediately before `target`.
void InsertBefore(Instruction* target, Instruction* new_instr) {
  Instruction* prev = target->GetPrevious();
  Instruction::Link(prev, new_instr);
  Instruction::Link(new_instr, target);
}

// Helper to insert `new_instr` immediately after `target`.
void InsertAfter(Instruction* target, Instruction* new_instr) {
  Instruction* next = target->GetNext();
  Instruction::Link(target, new_instr);
  Instruction::Link(new_instr, next);
}

// Returns true if `type` is a heap-allocated pointer or tagged value that participates in ARC.
auto TypeNeedsARC(const CompileType* type) -> bool {
  if (!type)
    return false;

  switch (type->GetKind()) {
    case CompileType::kNever:
    case CompileType::kNone:
    case CompileType::kBool:
    case CompileType::kInt:
    case CompileType::kFloat:
    case CompileType::kNumber:
#define DEFINE_CASE(Name) case CompileType::k##Name:
      FOR_EACH_COLOR_MODEL(DEFINE_CASE)
#undef DEFINE_CASE
    case CompileType::kVec2:
    case CompileType::kVec3:
    case CompileType::kRect:
    case CompileType::kPx:
    case CompileType::kPct:
    case CompileType::kFr:
    case CompileType::kHandle:
      return false;
    default:
      return true;  // kString, kSeq, kRecord, kNode, kFunction (closure), kAny, kUnion, kVariant
  }
}

// Returns true if operand `idx` of `instr` transfers ownership into `instr`.
auto InputIsConsumed(Instruction* instr, uint64_t idx) -> bool {
  if (instr->IsReturn() || instr->IsSeqAppend() || instr->IsUpdateRecord() || instr->IsNewList() ||
      instr->IsNewRecord() || instr->IsNewVariant() || instr->IsNewClosure() || instr->IsStoreLocal() ||
      instr->IsCall() || instr->IsRegisterEvent() || instr->IsNewTuple()) {
    return true;
  }
  if (instr->IsStoreProperty())
    return idx == StorePropertyInstr::kValuePos;  // Consumes value (1), borrows instance (0)
  if (instr->IsStoreIndex())
    return idx == StoreIndexInstr::kValuePos;
  return false;
}

void FlowGraph::InsertARC() {
  using DefSet = std::unordered_set<Definition*>;

  const auto insert_at_block_start = [](EntryInstr* entry_block, Instruction* new_instr) {
    Instruction* anchor = entry_block;
    while (anchor->GetNext() && anchor->GetNext()->IsPhi())
      anchor = anchor->GetNext();
    InsertAfter(anchor, new_instr);
  };

  // ── 1. Identify Borrowed Definitions ──────────────────────────────────────
  // `view`'s `state` parameter is borrowed by the host frame loop.
  if (name_ == "view" && !parameters_.empty()) {
    parameters_[0]->SetBorrowed(true);
  }

  DefSet borrowed{};
  for (ParameterInstr* param : parameters_) {
    if (param->IsBorrowed()) {
      borrowed.insert(param);
    }
  }

  // Propagate borrowed status through read-only projections (LoadProperty / LoadIndex)
  // in reverse postorder so reading fields/elements from a borrowed record/seq stays borrowed.
  for (EntryInstr* block : reverse_postorder_) {
    if (IsDead(block))
      continue;
    for (Instruction* instr = block->GetNext(); instr != nullptr; instr = instr->GetNext()) {
      if (const auto load = instr->AsLoadProperty()) {
        if (borrowed.contains(load->GetInstance()->GetDefinition()))
          borrowed.insert(load);
      } else if (const auto load = instr->AsLoadPropertySafely()) {
        if (borrowed.contains(load->GetInstance()->GetDefinition()))
          borrowed.insert(load);
      } else if (const auto load = instr->AsLoadIndex()) {
        if (borrowed.contains(load->GetInstance()->GetDefinition()))
          borrowed.insert(load);
      }
    }
  }

  const auto is_owned_arc = [&](Definition* def) -> bool {
    if (!def || def->IsStackAllocated() || def->IsConstant() || def->IsLoadFunction() || def->IsReturn())
      return false;
    if (borrowed.contains(def))
      return false;
    return TypeNeedsARC(def->GetCompileType());
  };

  // ── 2. Backward Liveness Analysis (Owned ARC Values Only) ─────────────────
  std::unordered_map<EntryInstr*, DefSet> live_in{};
  std::unordered_map<EntryInstr*, DefSet> live_out{};

  bool changed = true;
  while (changed) {
    changed = false;

    for (EntryInstr* block : postorder_) {
      if (IsDead(block))
        continue;

      DefSet out{};
      for (uint64_t i = 0; i < block->GetNumberOfSuccessors(); i++) {
        EntryInstr* succ = block->GetSuccessorAt(i);
        if (!succ || IsDead(succ))
          continue;

        out.insert(live_in[succ].begin(), live_in[succ].end());

        if (const auto join = succ->AsJoinEntry()) {
          for (PhiInstr* phi : join->GetPhis()) {
            out.erase(phi);
            for (const auto& in : phi->GetIncoming()) {
              if (in.predecessor == block && in.value) {
                if (Definition* def = in.value->GetDefinition(); is_owned_arc(def)) {
                  out.insert(def);
                }
              }
            }
          }
        }
      }

      DefSet live = out;
      for (Instruction* instr = GetTerminator(block); instr != nullptr && instr != block;
           instr = instr->GetPrevious()) {
        if (instr->IsPhi())
          continue;

        if (instr->IsDefinition() && !instr->IsReturn()) {
          live.erase(static_cast<Definition*>(instr));
        }

        for (uint64_t idx = 0; idx < instr->GetNumberOfInputs(); idx++) {
          if (Value* input = instr->GetInputAt(idx)) {
            if (Definition* def = input->GetDefinition(); is_owned_arc(def)) {
              live.insert(def);
            }
          }
        }
      }

      if (out != live_out[block] || live != live_in[block]) {
        live_out[block] = std::move(out);
        live_in[block] = std::move(live);
        changed = true;
      }
    }
  }

  // ── 3. Insert Retain / Release & Detect Destructive Property Moves ────────
  for (EntryInstr* block : reverse_postorder_) {
    if (IsDead(block))
      continue;

    DefSet live = live_out[block];

    // Record phi uses on outgoing edges as live at the end of `block`
    for (uint64_t i = 0; i < block->GetNumberOfSuccessors(); i++) {
      EntryInstr* succ = block->GetSuccessorAt(i);
      if (const auto join = succ ? succ->AsJoinEntry() : nullptr) {
        for (PhiInstr* phi : join->GetPhis()) {
          for (const auto& in : phi->GetIncoming()) {
            if (in.predecessor == block && in.value) {
              if (Definition* def = in.value->GetDefinition(); is_owned_arc(def)) {
                live.insert(def);
              }
            }
          }
        }
      }
    }

    // Track properties overwritten by a consuming `UpdateRecord` in this block
    std::unordered_map<Definition*, std::unordered_set<std::string>> destructive_overwrites{};

    // Walk backward through `block`
    for (Instruction* instr = GetTerminator(block); instr != nullptr && instr != block;) {
      Instruction* prev = instr->GetPrevious();
      if (instr->IsPhi()) {
        instr = prev;
        continue;
      }

      // Drop owned definitions that are dead immediately after creation (excluding ReturnInstr!)
      if (instr->IsDefinition() && !instr->IsReturn()) {
        auto* def = static_cast<Definition*>(instr);
        if (is_owned_arc(def) && !live.contains(def)) {
          InsertAfter(def, ReleaseInstr::New(Value::New(def)));
        }
        live.erase(def);
      }

      // Check if this is a consuming UpdateRecord (%new = UpdateRecord(%base, prop: %val))
      if (const auto upd = instr->AsUpdateRecord()) {
        Definition* base = upd->GetBase()->GetDefinition();
        if (is_owned_arc(base) && !live.contains(base)) {
          upd->SetMoveBase(true);
          for (const auto& entry : upd->GetEntries()) {
            destructive_overwrites[base].insert(std::string(entry.property->GetPropertyName()->GetData()));
          }
        }
      }

      // Check if this is a LoadProperty whose property is about to be destructively overwritten
      bool is_destructive_take = false;
      if (const auto load = instr->AsLoadProperty()) {
        Definition* base = load->GetInstance()->GetDefinition();
        const std::string prop_name(load->GetProperty()->GetPropertyName()->GetData());
        if (auto it = destructive_overwrites.find(base);
            it != destructive_overwrites.end() && it->second.contains(prop_name)) {
          load->SetMove(true);
          it->second.erase(prop_name);
          is_destructive_take = true;
        }
      }

      // Process inputs of `instr`
      DefSet retained_this_instr{};
      for (uint64_t idx = 0; idx < instr->GetNumberOfInputs(); idx++) {
        Value* input = instr->GetInputAt(idx);
        if (!input)
          continue;
        Definition* def = input->GetDefinition();
        if (!def)
          continue;

        const bool consumes = InputIsConsumed(instr, idx);

        // Case A: Borrowed value passed to a consuming instruction (e.g., returning a borrowed field)
        // -> Retain it to create a fresh owned reference!
        if (borrowed.contains(def) && TypeNeedsARC(def->GetCompileType())) {
          if (consumes && retained_this_instr.insert(def).second) {
            InsertBefore(instr, RetainInstr::New(Value::New(def)));
          }
          continue;
        }

        // Case B: Owned ARC value
        if (!is_owned_arc(def))
          continue;

        // Destructive TakeProperty borrows `base` without keeping `base` alive or bumping its refcount
        if (is_destructive_take && idx == LoadPropertyInstr::kInstancePos) {
          continue;
        }

        const bool is_last_use = !live.contains(def);
        if (consumes) {
          if (is_last_use) {
            live.insert(def);
          } else {
            if (retained_this_instr.insert(def).second) {
              InsertBefore(instr, RetainInstr::New(Value::New(def)));
            }
          }
        } else {
          if (is_last_use) {
            live.insert(def);
            if (!live_out[block].contains(def)) {
              InsertAfter(instr, ReleaseInstr::New(Value::New(def)));
            }
          }
        }
      }

      // At a conditional Branch, drop owned values live on one arm but dead on the other
      if (const auto branch = instr->AsBranch()) {
        EntryInstr* then_bb = branch->GetThen();
        EntryInstr* else_bb = branch->HasElse() ? static_cast<EntryInstr*>(branch->GetElse())
                                                : static_cast<EntryInstr*>(branch->GetJoin());
        if (then_bb && else_bb && then_bb != else_bb) {
          DefSet dropped_then{};
          DefSet dropped_else{};
          for (Definition* v : live_out[block]) {
            const bool in_then = live_in[then_bb].contains(v);
            const bool in_else = live_in[else_bb].contains(v);
            if (in_then && !in_else && dropped_else.insert(v).second) {
              insert_at_block_start(else_bb, ReleaseInstr::New(Value::New(v)));
            } else if (in_else && !in_then && dropped_then.insert(v).second) {
              insert_at_block_start(then_bb, ReleaseInstr::New(Value::New(v)));
            }
          }
        }
      }
      instr = prev;
    }
  }
}

auto FlowGraph::OptimizeCOWUpdates() -> bool {
  bool changed = false;

  // 1. Count definition uses (so we only fold single-use singleton lists)
  std::unordered_map<Definition*, uint32_t> use_counts{};
  for (const auto block : reverse_postorder_) {
    if (const auto join = block->AsJoinEntry()) {
      for (const auto phi : join->GetPhis()) {
        for (const auto& in : phi->GetIncoming()) {
          if (in.value && in.value->GetDefinition())
            use_counts[in.value->GetDefinition()]++;
        }
      }
    }

    for (Instruction* instr = block->GetNext(); instr != nullptr; instr = instr->GetNext()) {
      if (instr->IsPhi())
        continue;
      for (uint64_t i = 0; i < instr->GetNumberOfInputs(); i++) {
        if (const auto input = instr->GetInputAt(i); input && input->GetDefinition())
          use_counts[input->GetDefinition()]++;
      }
    }
  }

  // 2. Walk blocks in reverse postorder and rewrite COW patterns
  for (const auto block : reverse_postorder_) {
    for (Instruction* instr = block->GetNext(); instr != nullptr;) {
      // Save `next` before replacing/removing `instr`
      Instruction* next = instr->GetNext();

      // Pattern A: BinaryOp(kAdd, seq, NewList(elem)) -> SeqAppend(seq, elem)
      if (const auto bin = instr->AsBinaryOp(); bin && bin->GetOp() == BinaryOp::kAdd) {
        Definition* lhs = bin->GetLeft()->GetDefinition();
        Definition* rhs = bin->GetRight()->GetDefinition();

        if (lhs && lhs->HasCompileType() && lhs->GetCompileType()->IsSeq()) {
          if (const auto list = rhs ? rhs->AsNewList() : nullptr;
              list && list->GetNumberOfInputs() == 1 && use_counts[list] == 1) {
            auto* elem = list->GetInputAt(0)->GetDefinition();
            auto* append = SeqAppendInstr::New(Value::New(lhs), Value::New(elem));
            append->SetCompileType(bin->GetCompileType());

            SetHasCOWMutations(true);
            ReplaceAllUsesWith(bin, append);
            Replace(bin, append);
            Remove(list);
            changed = true;
          }
        }
      }

      // Pattern B: NewRecord(spread=%base) + Spread + StoreProperty... -> UpdateRecord
      else if (const auto rec = instr->AsNewRecord(); rec && rec->GetNumberOfInputs() == 1) {
        Definition* base = rec->GetInputAt(0)->GetDefinition();
        std::vector<UpdateRecordInstr::Entry> entries{};
        std::vector<Instruction*> to_remove{};

        // Scan forward in the same block for its Spread and StoreProperty initializers
        Instruction* scan = next;
        bool saw_spread = false;
        bool self_ref = false;
        while (scan != nullptr) {
          if (const auto spread = scan->AsSpread();
              spread && spread->GetInstance()->GetDefinition() == rec && spread->GetValue()->GetDefinition() == base) {
            saw_spread = true;
            to_remove.push_back(scan);
            scan = scan->GetNext();
            continue;
          }
          if (const auto store = scan->AsStoreProperty(); store && store->GetInstance()->GetDefinition() == rec) {
            if (store->GetValue()->GetDefinition() == rec) {
              self_ref = true;
              break;
            }

            entries.push_back({store->GetProperty(), Value::New(store->GetValue()->GetDefinition())});
            to_remove.push_back(scan);
            scan = scan->GetNext();
            continue;
          }
          break;
        }

        // Only fuse if we found the Spread + at least one StoreProperty and `rec` isn't used inside those values
        if (saw_spread && !self_ref && !entries.empty()) {
          auto* upd = UpdateRecordInstr::New(Value::New(base), std::move(entries));
          upd->SetCompileType(rec->GetCompileType());

          // Advance `next` past all removed Spread/StoreProperty instructions
          next = scan;

          ReplaceAllUsesWith(rec, upd);
          Replace(rec, upd);
          for (Instruction* dead : to_remove) {
            Remove(dead);
          }
          SetHasCOWMutations(true);
          changed = true;
        }
      }

      instr = next;
    }
  }

  return changed;
}

auto FlowGraph::DedupeLayoutSlotBegins() -> bool {
  bool changed = false;

  std::unordered_map<EntryInstr*, std::vector<EntryInstr*>> dom_children{};
  for (const auto block : preorder_) {
    if (EntryInstr* parent = GetImmediateDominator(block)) {
      dom_children[parent].push_back(block);
    }
  }

  const auto visit = [&](auto& self, EntryInstr* block, LayoutSlotBeginInstr* pending_begin) -> void {
    if (block != entry_ && block->GetNumberOfPredecessors() != 1)
      pending_begin = nullptr;

    for (Instruction* instr = block->GetNext(); instr != nullptr;) {
      Instruction* next = instr->GetNext();

      if (const auto begin = instr->AsLayoutSlotBegin()) {
        if (pending_begin && pending_begin->GetPlan() == begin->GetPlan() &&
            pending_begin->GetSlot() == begin->GetSlot()) {
          Remove(pending_begin);
          changed = true;
        }
        pending_begin = begin;
      } else if (instr->IsLayoutSlotInstance() || instr->IsLayoutSlotEnd() || instr->IsCall()) {
        pending_begin = nullptr;
      }

      instr = next;
    }

    for (EntryInstr* child : dom_children[block])
      self(self, child, pending_begin);
  };

  visit(visit, entry_, nullptr);
  return changed;
}

auto FlowGraph::EliminateDeadCode() -> bool {
  bool any_changed = false;
  bool changed = true;

  while (changed) {
    changed = false;

    // 1. Count uses of every Definition across all reachable blocks
    std::unordered_map<Definition*, uint32_t> use_counts{};
    for (const auto block : reverse_postorder_) {
      if (const auto join = block->AsJoinEntry()) {
        for (const auto phi : join->GetPhis()) {
          for (const auto& in : phi->GetIncoming()) {
            if (in.value && in.value->GetDefinition())
              use_counts[in.value->GetDefinition()]++;
          }
        }
      }

      for (Instruction* instr = block->GetNext(); instr != nullptr; instr = instr->GetNext()) {
        if (instr->IsPhi())
          continue;
        for (uint64_t i = 0; i < instr->GetNumberOfInputs(); i++) {
          if (const auto input = instr->GetInputAt(i); input && input->GetDefinition())
            use_counts[input->GetDefinition()]++;
        }
      }
    }

    // 2. Sweep unused pure definitions in reverse order
    for (auto it = postorder_.begin(); it != postorder_.end(); ++it) {
      EntryInstr* block = *it;
      for (Instruction* instr = GetTerminator(block); instr != nullptr && instr != block;) {
        Instruction* prev = instr->GetPrevious();
        if (instr->IsDefinition()) {
          auto* defn = dynamic_cast<Definition*>(instr);
          if (use_counts[defn] == 0 && HasNoSideEffects(instr)) {
            Remove(instr);
            changed = true;
            any_changed = true;
          }
        }
        instr = prev;
      }
    }
  }

  return any_changed;
}

auto FlowGraph::FoldEmptyBranches() -> bool {
  bool changed = false;

  for (const auto block : reverse_postorder_) {
    if (block->IsGraphEntry())
      continue;

    Instruction* term = GetTerminator(block);
    const auto branch = term ? term->AsBranch() : nullptr;
    if (!branch)
      continue;

    EntryInstr* then_block = branch->GetThen();
    EntryInstr* else_block =
        branch->HasElse() ? static_cast<EntryInstr*>(branch->GetElse()) : static_cast<EntryInstr*>(branch->GetJoin());
    EntryInstr* join_block = branch->GetJoin();

    // Case 1: Both arms are empty jumps to the exact same join block
    if (join_block && IsEmptyJumpBlock(then_block, join_block) && IsEmptyJumpBlock(else_block, join_block)) {
      // Ensure join_block has no Phis distinguishing between then_block and else_block
      if (const auto join = join_block->AsJoinEntry(); !join || join->GetPhis().empty()) {
        Instruction* prev = branch->GetPrevious();
        Remove(branch);
        Instruction::Link(prev, GotoInstr::New(join_block));
        changed = true;
      }
    }

    // Case 2: Constant boolean condition (e.g., Branch(true, ...))
    else if (const auto cond = branch->GetCondition()->GetDefinition()->AsConstant()) {
      if (cond->GetValue() && cond->GetValue()->GetType() == BuiltinTypes::kBoolType) {
        const bool is_true = static_cast<Bool*>(cond->GetValue())->GetValue();
        EntryInstr* taken = is_true ? then_block : else_block;
        if (taken) {
          Instruction* prev = branch->GetPrevious();
          Remove(branch);
          Instruction::Link(prev, GotoInstr::New(taken));
          changed = true;
        }
      }
    }
  }

  return changed;
}

auto FlowGraph::IsExtern() const -> bool {
  return function_ && function_->IsExtern();
}

auto FlowGraph::GetTerminator(EntryInstr* block) -> Instruction* {
  Instruction* last = block;
  while (last->HasNext())
    last = last->GetNext();
  return last;
}

void FlowGraph::EliminateDeadPaints() {
  std::unordered_set<Definition*> dead_paints{};

  for (const auto block : GetReversePostorder()) {
    for (Instruction* instr = block->GetNext(); instr != nullptr;) {
      Instruction* next = instr->GetNext();

      bool is_dead = false;
      if (const auto fill = instr->AsFillRect()) {
        Definition* color = fill->GetInputAt(1)->GetDefinition();
        is_dead = IsZeroAlphaConstant(color);
      } else if (const auto box = instr->AsPaintBox()) {
        Definition* fill = box->GetInputAt(1)->GetDefinition();
        Definition* border_color = box->GetInputAt(2)->GetDefinition();
        Definition* border_width = box->GetInputAt(3)->GetDefinition();
        Definition* opacity = box->GetInputAt(5)->GetDefinition();

        const bool transparent_opacity = IsZeroFloatConstant(opacity);
        const bool no_fill = IsZeroAlphaConstant(fill);
        const bool no_border = IsZeroFloatConstant(border_width) || IsZeroAlphaConstant(border_color);
        is_dead = transparent_opacity || (no_fill && no_border);
      } else if (const auto check = instr->AsCheckRendererStatus()) {
        Definition* target = check->GetValue()->GetDefinition();
        is_dead = dead_paints.contains(target);
      }

      if (is_dead) {
        if (instr->IsDefinition())
          dead_paints.insert(dynamic_cast<Definition*>(instr));

        Remove(instr);
      }

      instr = next;
    }
  }
}

void FlowGraph::DedupeConstants() {
  std::unordered_map<std::string, ConstantInstr*> pool{};
  EntryInstr* entry_block = nullptr;
  for (EntryInstr* b : reverse_postorder_) {
    if (!b->IsGraphEntry() && b->GetNext() != nullptr) {
      entry_block = b;
      break;
    }
  }

  if (!entry_block)
    return;

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
  FoldConstantStrings();
  EliminateDeadPaints();

  bool changed = true;
  while (changed) {
    changed = false;
    if (FoldEmptyBranches()) {
      RebuildCFG();
      changed = true;
    }

    if (SimplifyCFG()) {
      RebuildCFG();
      changed = true;
    }

    if (EliminateCommonSubexpressions())
      changed = true;
    if (EliminateRedundantClips())
      changed = true;
    if (DedupeLayoutSlotBegins())
      changed = true;
    if (EliminateDeadCode())
      changed = true;

    if (EliminateEmptyLoops()) {
      RebuildCFG();
      changed = true;
    }
  }
}

void FlowGraph::RebuildCFG() {
  for (const auto block : preorder_) {
    block->ClearSuccessors();
    block->ClearPredecessors();
  }

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

  for (const auto block : preorder_) {
    if (const auto join = block->AsJoinEntry()) {
      for (const auto phi : join->GetPhis()) {
        auto& incoming = phi->GetIncoming();
        std::erase_if(incoming, [&](const auto& in) {
          return !visited.contains(in.predecessor);
        });
      }
    }
  }
}

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
