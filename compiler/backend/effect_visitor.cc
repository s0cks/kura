#include "backend/effect_visitor.h"

#include "backend/flow_graph.h"
#include "backend/ir.h"
#include "backend/value_visitor.h"
#include "function.h"
#include "local_scope.h"

namespace kura {
void EffectVisitor::ReturnDefinition(Definition* defn) {
  if (WantsValue()) {
    ReturnValue(Bind(defn));
  } else if (!defn->IsConstant()) {
    Emit(defn);
  }
}

auto EffectVisitor::Eval(expr::Expr* expr) -> Value* {
  if (!expr) {
    Fail("missing expression");
    return nullptr;
  }

  ValueVisitor for_expr(owner_);
  if (!expr->Accept(&for_expr))
    return nullptr;

  if (!for_expr.HasValue()) {
    Fail(std::string("expression produced no value: ") + std::string(expr->GetName()));
    return nullptr;
  }

  return for_expr.GetValue();
}

auto EffectVisitor::EvalForEffect(expr::Expr* expr) -> bool {
  if (!expr)
    return true;

  EffectVisitor for_expr(owner_);
  return expr->Accept(&for_expr);
}

auto EffectVisitor::EvalArm(expr::Expr* expr, Value** value) -> bool {
  *value = nullptr;
  if (!WantsValue())
    return EvalForEffect(expr);

  if (!expr) {
    *value = NoneValue();
    return true;
  }

  *value = Eval(expr);
  return *value != nullptr;
}

auto EffectVisitor::EvalList(const expr::ExprList& exprs, ValueList* values) -> bool {
  for (const auto expr : exprs) {
    const auto value = Eval(expr);
    if (!value)
      return false;

    values->push_back(value);
  }

  return true;
}

auto EffectVisitor::VisitLiteral(expr::LiteralExpr* expr) -> VisitResult {
  ReturnDefinition(ConstantInstr::New(expr->GetValue()));
  return true;
}

auto EffectVisitor::VisitUnary(expr::UnaryExpr* expr) -> VisitResult {
  const auto value = Eval(expr->GetValue());
  if (!value)
    return false;

  ReturnDefinition(UnaryOpInstr::New(expr->GetOp(), value));
  return true;
}

auto EffectVisitor::LowerShortCircuit(expr::BinaryExpr* expr) -> VisitResult {
  const auto left = Eval(expr->GetLeft());
  if (!left)
    return false;

  const auto then_block = owner_->NewTarget("sc.then");
  const auto else_block = owner_->NewTarget("sc.else");
  const auto join = owner_->NewJoin("sc.join");
  owner_->Terminate(BranchInstr::New(left, then_block, else_block, join));

  const auto is_and = expr->IsBinaryAnd();
  Value* then_value = nullptr;
  Value* else_value = nullptr;

  owner_->Enter(then_block);
  if (is_and) {
    if (!(then_value = Eval(expr->GetRight())))
      return false;
  } else {
    then_value = left;
  }

  const auto then_end = owner_->block_;
  owner_->Terminate(GotoInstr::New(join));

  owner_->Enter(else_block);
  if (is_and) {
    else_value = left;
  } else {
    if (!(else_value = Eval(expr->GetRight())))
      return false;
  }

  const auto else_end = owner_->block_;
  owner_->Terminate(GotoInstr::New(join));

  owner_->Enter(join);
  const auto phi = join->NewPhi();
  phi->AddIncoming(then_end, then_value);
  phi->AddIncoming(else_end, else_value);
  ReturnValue(Bind(phi));
  return true;
}

auto EffectVisitor::VisitBinary(expr::BinaryExpr* expr) -> VisitResult {
  if (expr->IsBinaryAnd() || expr->IsBinaryOr())
    return LowerShortCircuit(expr);

  if (expr->IsPipe())
    return Fail("a pipe should have been lowered to a call by the frontend");

  const auto left = Eval(expr->GetLeft());
  if (!left)
    return false;

  const auto right = Eval(expr->GetRight());
  if (!right)
    return false;

  ReturnDefinition(BinaryOpInstr::New(expr->GetOp(), left, right));
  return true;
}

auto EffectVisitor::VisitSeq(expr::SeqExpr* expr) -> VisitResult {
  const auto& children = expr->GetChildren();
  if (children.empty()) {
    if (WantsValue())
      ReturnValue(NoneValue());

    return true;
  }

  for (auto idx = 0; idx < children.size() - 1; idx++) {
    if (!EvalForEffect(children[idx]))
      return false;
  }

  const auto last = children.back();
  if (!WantsValue())
    return EvalForEffect(last);

  const auto value = Eval(last);
  if (!value)
    return false;

  ReturnValue(value);
  return true;
}

auto EffectVisitor::VisitIf(expr::IfExpr* expr) -> VisitResult {
  const auto condition = Eval(expr->GetCondition());
  if (!condition)
    return false;

  const auto has_else = expr->HasElse();
  const auto then_block = owner_->NewTarget("if.then");
  const auto else_block = (has_else || WantsValue()) ? owner_->NewTarget("if.else") : nullptr;
  const auto join = owner_->NewJoin("if.join");
  owner_->Terminate(BranchInstr::New(condition, then_block, else_block, join));

  owner_->Enter(then_block);
  Value* then_value = nullptr;
  if (!EvalArm(expr->GetThen(), &then_value))
    return false;

  const auto then_end = owner_->block_;
  owner_->Terminate(GotoInstr::New(join));

  Value* else_value = nullptr;
  EntryInstr* else_end = nullptr;
  if (else_block) {
    owner_->Enter(else_block);
    if (!EvalArm(expr->GetElse(), &else_value))
      return false;

    else_end = owner_->block_;
    owner_->Terminate(GotoInstr::New(join));
  }

  owner_->Enter(join);
  if (WantsValue()) {
    const auto phi = join->NewPhi();
    phi->AddIncoming(then_end, then_value);
    phi->AddIncoming(else_end, else_value);
    ReturnValue(Bind(phi));
  }

  return true;
}

auto EffectVisitor::VisitCase(expr::CaseExpr*) -> VisitResult {
  return Fail("a case can only appear in a match");
}

auto EffectVisitor::LowerPattern(expr::PatternExpr* pattern, Value* subject, TargetEntryInstr* fail, bool* irrefutable)
    -> bool {
  *irrefutable = false;
  if (pattern->IsWildcardPattern()) {
    *irrefutable = true;
    return true;
  }

  if (const auto binding = pattern->AsBindingPattern()) {
    owner_->Define(binding->GetLocal(), subject->GetDefinition());
    *irrefutable = true;
    return true;
  }

  if (const auto literal = pattern->AsLiteralPattern()) {
    const auto value = Bind(ConstantInstr::New(literal->GetLiteral()->GetValue()));
    const auto matches = Bind(BinaryOpInstr::New(BinaryOp::kEq, subject, value));
    const auto next = owner_->NewTarget("pattern.next");
    owner_->Terminate(BranchInstr::New(matches, next, fail, nullptr));
    owner_->Enter(next);
    return true;
  }

  if (const auto variant = pattern->AsVariantPattern()) {
    const auto matches = Bind(TestVariantInstr::New(subject, variant->GetVariantName()));
    const auto next = owner_->NewTarget("pattern.next");
    owner_->Terminate(BranchInstr::New(matches, next, fail, nullptr));
    owner_->Enter(next);
    if (variant->HasPayload()) {
      const auto payload = Bind(LoadVariantPayloadInstr::New(subject, variant->GetVariantName()));
      bool payload_irrefutable = false;
      return LowerPattern(variant->GetPayload(), payload, fail, &payload_irrefutable);
    }

    return true;
  }

  Fail("unsupported pattern");
  return false;
}

auto EffectVisitor::VisitMatch(expr::MatchExpr* expr) -> VisitResult {
  const auto subject = Eval(expr->GetSubject());
  if (!subject)
    return false;

  struct Incoming {
    EntryInstr* block;
    Value* value;
  };
  std::vector<Incoming> incoming{};
  const auto join = owner_->NewJoin("match.join");

  bool exhausted = false;
  for (const auto c : expr->GetCases()) {
    const auto fail = owner_->NewTarget("match.next");
    bool irrefutable = false;
    if (!LowerPattern(c->GetPattern(), subject, fail, &irrefutable))
      return false;

    Value* value = nullptr;
    if (!EvalArm(c->GetBody(), &value))
      return false;

    incoming.push_back({owner_->block_, value});
    owner_->Terminate(GotoInstr::New(join));

    if (irrefutable) {
      exhausted = true;
      break;
    }

    owner_->Enter(fail);
  }

  if (!exhausted) {
    Value* value = nullptr;
    if (expr->HasWildcard()) {
      if (!EvalArm(expr->GetWildcard(), &value))
        return false;
    } else if (WantsValue()) {
      value = NoneValue();
    }

    incoming.push_back({owner_->block_, value});
    owner_->Terminate(GotoInstr::New(join));
  }

  owner_->Enter(join);
  if (WantsValue()) {
    const auto phi = join->NewPhi();
    for (const auto& in : incoming)
      phi->AddIncoming(in.block, in.value);

    ReturnValue(Bind(phi));
  }

  return true;
}

auto EffectVisitor::VisitNode(expr::NodeExpr* expr) -> VisitResult {
  const auto node = Bind(NewNodeInstr::New(static_cast<NewNodeInstr::Kind>(expr->GetKind())));
  for (const auto property : expr->GetProperties()) {
    const auto value = Eval(property->GetValue());
    if (!value)
      return false;

    Emit(StorePropertyInstr::New(node, property->GetProperty(), value));
  }

  int64_t index = 0;
  for (const auto child : expr->GetChildren()) {
    if (child->IsStoreLocal()) {
      if (!EvalForEffect(child))
        return false;

      continue;
    }

    const auto value = Eval(child);
    if (!value)
      return false;

    const auto at = Bind(ConstantInstr::New(Int::New(index++)));
    Emit(StoreIndexInstr::New(node, at, value));
  }

  if (WantsValue())
    ReturnValue(node);

  return true;
}

auto EffectVisitor::VisitPipeline(expr::PipelineExpr* expr) -> VisitResult {
  const auto& children = expr->GetChildren();
  if (children.empty())
    return Fail("empty pipeline");

  auto value = Eval(children.front());
  if (!value)
    return false;

  for (auto idx = 1; idx < children.size(); idx++) {
    ValueList args{value};
    Value* target = nullptr;
    if (const auto call = children[idx]->AsCall()) {
      if (!(target = Eval(call->GetTarget())))
        return false;

      if (!EvalList(call->GetArgs(), &args))
        return false;
    } else if (!(target = Eval(children[idx]))) {
      return false;
    }

    value = Bind(CallInstr::New(target, args));
  }

  ReturnValue(value);
  return true;
}

auto EffectVisitor::VisitList(expr::ListExpr* expr) -> VisitResult {
  ValueList values{};
  if (!EvalList(expr->GetChildren(), &values))
    return false;

  ReturnDefinition(NewListInstr::New(values));
  return true;
}

auto EffectVisitor::BeginLoop(Value* iterable, Loop* loop) -> bool {
  loop->iterable = iterable;
  loop->length = Bind(LoadLengthInstr::New(iterable));
  const auto zero = Bind(ConstantInstr::New(Int::New(0)));
  loop->preheader = owner_->block_;
  loop->header = owner_->NewJoin("loop.header");
  loop->exit = owner_->NewTarget("loop.exit");
  const auto body = owner_->NewTarget("loop.body");
  owner_->Terminate(GotoInstr::New(loop->header));

  owner_->Enter(loop->header);
  loop->index = loop->header->NewPhi();
  loop->index->AddIncoming(loop->preheader, zero);
  const auto index = Bind(loop->index);
  const auto in_range = Bind(BinaryOpInstr::New(BinaryOp::kLessThan, index, loop->length));
  owner_->Terminate(BranchInstr::New(in_range, body, loop->exit, nullptr));

  owner_->Enter(body);
  loop->item = Bind(LoadIndexInstr::New(iterable, Value::New(loop->index)));
  return true;
}

void EffectVisitor::EndLoop(Loop* loop) {
  const auto one = Bind(ConstantInstr::New(Int::New(1)));
  const auto next = Bind(BinaryOpInstr::New(BinaryOp::kAdd, Value::New(loop->index), one));
  loop->index->AddIncoming(owner_->block_, next);
  owner_->Terminate(GotoInstr::New(loop->header));
  owner_->Enter(loop->exit);
}

auto EffectVisitor::VisitListComprehension(expr::ListComprehensionExpr* expr) -> VisitResult {
  const auto iterable = Eval(expr->GetIterable());
  if (!iterable)
    return false;

  const auto list = Bind(NewListInstr::New({}));

  Loop loop{};
  if (!BeginLoop(iterable, &loop))
    return false;

  owner_->Define(expr->GetLocal(), loop.item->GetDefinition());

  const auto append = [&]() -> bool {
    const auto value = Eval(expr->GetBody());
    if (!value)
      return false;

    const auto at = Bind(LoadLengthInstr::New(list));
    Emit(StoreIndexInstr::New(list, at, value));
    return true;
  };

  if (expr->HasClause()) {
    const auto condition = Eval(expr->GetClause());
    if (!condition)
      return false;

    const auto keep = owner_->NewTarget("guard.keep");
    const auto skip = owner_->NewTarget("guard.skip");
    const auto latch = owner_->NewJoin("guard.latch");
    owner_->Terminate(BranchInstr::New(condition, keep, skip, latch));
    owner_->Enter(keep);
    if (!append())
      return false;

    owner_->Terminate(GotoInstr::New(latch));
    owner_->Enter(skip);
    owner_->Terminate(GotoInstr::New(latch));
    owner_->Enter(latch);
  } else if (!append()) {
    return false;
  }

  EndLoop(&loop);
  if (WantsValue())
    ReturnValue(list);

  return true;
}

auto EffectVisitor::VisitFold(expr::FoldExpr* expr) -> VisitResult {
  const auto iterable = Eval(expr->GetIterable());
  if (!iterable)
    return false;

  const auto initial = Eval(expr->GetInitial());
  if (!initial)
    return false;

  Loop loop{};
  if (!BeginLoop(iterable, &loop))
    return false;

  const auto accumulator = loop.header->NewPhi();
  accumulator->AddIncoming(loop.preheader, initial);
  {
    Instruction* first = loop.index;
    Instruction::Link(loop.header, accumulator);
    Instruction::Link(accumulator, first);
  }
  owner_->Define(expr->GetBinding(), loop.item->GetDefinition());
  owner_->Define(expr->GetAccumulator(), accumulator);

  Value* next = nullptr;
  if (expr->HasClause()) {
    const auto condition = Eval(expr->GetClause());
    if (!condition)
      return false;

    const auto keep = owner_->NewTarget("guard.keep");
    const auto skip = owner_->NewTarget("guard.skip");
    const auto latch = owner_->NewJoin("guard.latch");
    owner_->Terminate(BranchInstr::New(condition, keep, skip, latch));

    owner_->Enter(keep);
    const auto value = Eval(expr->GetBody());
    if (!value)
      return false;

    const auto keep_end = owner_->block_;
    owner_->Terminate(GotoInstr::New(latch));

    owner_->Enter(skip);
    const auto skip_end = owner_->block_;
    owner_->Terminate(GotoInstr::New(latch));

    owner_->Enter(latch);
    const auto phi = latch->NewPhi();
    phi->AddIncoming(keep_end, value);
    phi->AddIncoming(skip_end, Value::New(accumulator));
    next = Bind(phi);
  } else {
    next = Eval(expr->GetBody());
    if (!next)
      return false;
  }

  accumulator->AddIncoming(owner_->block_, next);
  EndLoop(&loop);
  if (WantsValue())
    ReturnValue(Value::New(accumulator));

  return true;
}

auto EffectVisitor::VisitRecord(expr::RecordExpr* expr) -> VisitResult {
  const auto record = Bind(NewRecordInstr::New());
  for (const auto entry : expr->GetEntries()) {
    if (const auto spread = entry->AsSpread()) {
      const auto value = Eval(spread->GetValue());
      if (!value)
        return false;

      Emit(SpreadInstr::New(record, value));
    } else if (const auto property = entry->AsStoreProperty()) {
      const auto value = Eval(property->GetValue());
      if (!value)
        return false;

      Emit(StorePropertyInstr::New(record, property->GetProperty(), value));
    } else {
      return Fail("unexpected record entry");
    }
  }

  if (WantsValue())
    ReturnValue(record);

  return true;
}

auto EffectVisitor::VisitSpread(expr::SpreadExpr*) -> VisitResult {
  return Fail("a spread can only appear in a record");
}

auto EffectVisitor::VisitStoreProperty(expr::StorePropertyExpr*) -> VisitResult {
  return Fail("a property can only be set in a record or node");
}

auto EffectVisitor::VisitLoadLocal(expr::LoadLocalExpr* expr) -> VisitResult {
  const auto local = expr->GetLocal();
  if (FlowGraphBuilder::IsGlobal(local)) {
    if (const auto function = owner_->FindFunction(local)) {
      ReturnDefinition(LoadFunctionInstr::New(function));
    } else {
      ReturnDefinition(LoadLocalInstr::New(local));
    }

    return true;
  }

  const auto defn = owner_->Lookup(local);
  if (!defn)
    return Fail("use of unbound local `" + std::string(local->GetName()->GetData()) + "`");

  ReturnValue(Value::New(defn));
  return true;
}

auto EffectVisitor::VisitStoreLocal(expr::StoreLocalExpr* expr) -> VisitResult {
  const auto value = Eval(expr->GetValue());
  if (!value)
    return false;

  const auto local = expr->GetLocal();
  if (FlowGraphBuilder::IsGlobal(local)) {
    Emit(StoreLocalInstr::New(local, value));
  } else {
    owner_->Define(local, value->GetDefinition());
  }

  if (WantsValue())
    ReturnValue(NoneValue());

  return true;
}

auto EffectVisitor::VisitGetProperty(expr::GetPropertyExpr* expr) -> VisitResult {
  const auto instance = Eval(expr->GetInstance());
  if (!instance)
    return false;

  if (expr->IsSafeAccess()) {
    ReturnDefinition(LoadPropertySafelyInstr::New(instance, expr->GetProperty()));
  } else {
    ReturnDefinition(LoadPropertyInstr::New(instance, expr->GetProperty()));
  }

  return true;
}

auto EffectVisitor::VisitWildcardPattern(expr::WildcardPatternExpr*) -> VisitResult {
  return Fail("a pattern can only appear in a match");
}

auto EffectVisitor::VisitLiteralPattern(expr::LiteralPatternExpr*) -> VisitResult {
  return Fail("a pattern can only appear in a match");
}

auto EffectVisitor::VisitBindingPattern(expr::BindingPatternExpr*) -> VisitResult {
  return Fail("a pattern can only appear in a match");
}

auto EffectVisitor::VisitVariantPattern(expr::VariantPatternExpr*) -> VisitResult {
  return Fail("a pattern can only appear in a match");
}

auto EffectVisitor::VisitLambda(expr::LambdaExpr* expr) -> VisitResult {
  const auto captures = CollectCaptures(expr);
  ValueList values{};
  for (const auto local : captures) {
    const auto defn = owner_->Lookup(local);
    if (!defn)
      return Fail("lambda captures unbound local `" + std::string(local->GetName()->GetData()) + "`");

    values.push_back(Value::New(defn));
  }

  const auto graph = FlowGraphBuilder::BuildLambda(owner_->ctx_, expr, captures);
  if (!graph)
    return false;

  ReturnDefinition(NewClosureInstr::New(graph, values));
  return true;
}

auto EffectVisitor::VisitVariant(expr::VariantExpr* expr) -> VisitResult {
  Value* payload = nullptr;
  if (expr->HasPayload() && !(payload = Eval(expr->GetPayload())))
    return false;

  ReturnDefinition(NewVariantInstr::New(expr->GetVariantName(), payload));
  return true;
}

auto EffectVisitor::VisitCall(expr::CallExpr* expr) -> VisitResult {
  const auto target = Eval(expr->GetTarget());
  if (!target)
    return false;

  ValueList args{};
  if (!EvalList(expr->GetArgs(), &args))
    return false;

  ReturnDefinition(CallInstr::New(target, args));
  return true;
}
}  // namespace kura
