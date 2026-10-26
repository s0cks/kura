#include "backend/flow_graph_builder.h"

#include <iostream>
#include <sstream>
#include <unordered_set>

#include "backend/effect_visitor.h"
#include "backend/flow_graph.h"
#include "backend/ir.h"
#include "backend/value_visitor.h"
#include "common.h"
#include "frontend/expr.h"
#include "frontend/expr_printer.h"
#include "function.h"
#include "gir.h"
#include "local_scope.h"
#include "module.h"
#include "node_visitor.h"

namespace kura {
FlowGraphBuilder::FlowGraphBuilder(Context* ctx, const std::string& name, Function* function) :
  ctx_(ctx),
  name_(name),
  function_(function) {
  target_ = NewTarget("entry");
  entry_ = GraphEntryInstr::New(target_);
  entry_->SetBlockId(current_block_id_++);
  graph_ = FlowGraph::New(name, function, entry_);
  Enter(target_);
}

void FlowGraphBuilder::Emit(Instruction* instr) {
  if (!tail_)  // unreachable
    return;

  Instruction::Link(tail_, instr);
  tail_ = instr;
}

auto FlowGraphBuilder::Bind(Definition* defn) -> Value* {
  Emit(defn);
  return Value::New(defn);
}

void FlowGraphBuilder::Terminate(Instruction* instr) {
  Emit(instr);
  tail_ = nullptr;
}

void FlowGraphBuilder::Enter(EntryInstr* block) {
  block_ = block;
  tail_ = block;
}

auto FlowGraphBuilder::Lookup(LocalVariable* local) const -> Definition* {
  const auto it = env_.find(local);
  return it == env_.end() ? nullptr : it->second;
}

auto FlowGraphBuilder::IsGlobal(LocalVariable* local) -> bool {
  return local->HasOwner() && local->GetOwner()->IsRoot();
}

auto FlowGraphBuilder::FindFunction(LocalVariable* local) const -> Function* {
  if (!IsGlobal(local))
    return nullptr;

  const auto it = ctx_->functions.find(std::string(local->GetName()->GetData()));
  return it == ctx_->functions.end() ? nullptr : it->second;
}

auto FlowGraphBuilder::Fail(const std::string& message) -> VisitResult {
  ctx_->errors.push_back(name_ + ": " + message);
  return VisitResult::kStop;
}

void FlowGraphBuilder::AddParameters(const std::vector<LocalVariable*>& params,
                                     const std::vector<LocalVariable*>& captures) {
  uint64_t index = 0;
  for (const auto local : params) {
    const auto param = ParameterInstr::New(local, index++);
    graph_->AddParameter(param);
    Define(local, param);
    Emit(param);
  }

  for (const auto local : captures) {
    const auto param = ParameterInstr::New(local, index++, true);
    graph_->AddParameter(param);
    Define(local, param);
    Emit(param);
  }

  graph_->SetCaptures(captures);
}

auto FlowGraphBuilder::BuildViewBody(expr::Expr* body) -> bool {
  std::cout << "building view body" << std::endl;
  expr::ExprPrinter printer(std::cout, Indent{});
  if (!body->Accept(&printer))
    return false;
  std::cout << std::endl;

  {
    const auto defn = BeginFrameInstr::New(1024, 1024);
#ifdef KURA_DEBUG
    Emit(CheckRendererStatusInstr::New(Bind(defn)));
#else
    Emit(defn);
#endif  // KURA_DEBUG
  }

  NodeVisitor for_body(this);
  if (!body->Accept(&for_body))
    return false;

  {
    const auto defn = EndFrameInstr::New();
#ifdef KURA_DEBUG
    Emit(CheckRendererStatusInstr::New(Bind(defn)));
#else
    Emit(defn);
#endif  // KURA_DEBUG
  }

  Value* value = for_body.GetValue();
  if (!value)
    value = Bind(ConstantInstr::New(None::Get()));
  Terminate(ReturnInstr::New(value));
  return true;
}

auto FlowGraphBuilder::BuildBody(expr::Expr* body) -> bool {
  if (name_ == "view")
    return BuildViewBody(body);

  ValueVisitor for_body(this);
  if (!body->Accept(&for_body))
    return false;

  Value* value = for_body.GetValue();
  if (!value)
    value = Bind(ConstantInstr::New(None::Get()));

  Terminate(ReturnInstr::New(value));
  return true;
}

auto FlowGraphBuilder::BuildInit(const std::vector<expr::StoreLocalExpr*>& bindings) -> bool {
  for (const auto binding : bindings) {
    EffectVisitor for_binding(this);
    if (!binding->Accept(&for_binding))
      return false;
  }

  Terminate(ReturnInstr::New(Bind(ConstantInstr::New(None::Get()))));
  return true;
}

auto FlowGraphBuilder::Finish() -> FlowGraph* {
  graph_->Finalize();
  std::string error;
  if (!graph_->Verify(&error))
    ctx_->errors.push_back("internal error: " + error);

  return graph_;
}

namespace {
// Finds the locals a lambda closes over: those it reads but that are neither
// bound inside it (parameters, `let`s, patterns, loop variables) nor module
// level (functions and top-level bindings are reachable from anywhere).
class FreeLocals : public expr::ExprVisitor {
 public:
  std::unordered_set<LocalVariable*> uses{};
  std::unordered_set<LocalVariable*> defs{};
  std::vector<LocalVariable*> order{};  // first use, for a stable capture order

  inline void Use(LocalVariable* local) {
    if (uses.insert(local).second)
      order.push_back(local);
  }

  inline auto Walk(expr::Expr* expr) -> VisitResult {
    return expr ? expr->Accept(this) : VisitResult(VisitResult::kContinue);
  }

  auto VisitLiteral(expr::LiteralExpr*) -> VisitResult override {
    return true;
  }

  auto VisitUnary(expr::UnaryExpr* expr) -> VisitResult override {
    return Walk(expr->GetValue());
  }

  auto VisitBinary(expr::BinaryExpr* expr) -> VisitResult override {
    return Walk(expr->GetLeft()) && Walk(expr->GetRight());
  }

  auto VisitSeq(expr::SeqExpr* expr) -> VisitResult override {
    for (const auto child : expr->GetChildren()) {
      if (!Walk(child))
        return false;
    }

    return true;
  }

  auto VisitIf(expr::IfExpr* expr) -> VisitResult override {
    return Walk(expr->GetCondition()) && Walk(expr->GetThen()) && Walk(expr->GetElse());
  }

  auto VisitCase(expr::CaseExpr* expr) -> VisitResult override {
    return Walk(expr->GetPattern()) && Walk(expr->GetBody());
  }

  auto VisitMatch(expr::MatchExpr* expr) -> VisitResult override {
    if (!Walk(expr->GetSubject()))
      return false;

    for (const auto c : expr->GetCases()) {
      if (!Walk(c))
        return false;
    }

    return Walk(expr->GetWildcard());
  }

  auto VisitNode(expr::NodeExpr* expr) -> VisitResult override {
    for (const auto prop : expr->GetProperties()) {
      if (!Walk(prop))
        return false;
    }

    if (expr->IsText())
      std::cout << "building text node" << std::endl;

    for (const auto child : expr->GetChildren()) {
      if (expr->IsText())
        std::cout << "child: " << child->GetName() << std::endl;
      if (!Walk(child))
        return false;
    }

    return true;
  }

  auto VisitPipeline(expr::PipelineExpr* expr) -> VisitResult override {
    for (const auto child : expr->GetChildren()) {
      if (!Walk(child))
        return false;
    }

    return true;
  }

  auto VisitList(expr::ListExpr* expr) -> VisitResult override {
    for (const auto child : expr->GetChildren()) {
      if (!Walk(child))
        return false;
    }

    return true;
  }

  auto VisitListComprehension(expr::ListComprehensionExpr* expr) -> VisitResult override {
    defs.insert(expr->GetLocal());
    return Walk(expr->GetIterable()) && Walk(expr->GetClause()) && Walk(expr->GetBody());
  }

  auto VisitRecord(expr::RecordExpr* expr) -> VisitResult override {
    for (const auto entry : expr->GetEntries()) {
      if (!Walk(entry))
        return false;
    }

    return true;
  }

  auto VisitSpread(expr::SpreadExpr* expr) -> VisitResult override {
    return Walk(expr->GetValue());
  }

  auto VisitLoadLocal(expr::LoadLocalExpr* expr) -> VisitResult override {
    Use(expr->GetLocal());
    return true;
  }

  auto VisitStoreLocal(expr::StoreLocalExpr* expr) -> VisitResult override {
    defs.insert(expr->GetLocal());
    return Walk(expr->GetValue());
  }

  auto VisitGetProperty(expr::GetPropertyExpr* expr) -> VisitResult override {
    return Walk(expr->GetInstance());
  }

  auto VisitStoreProperty(expr::StorePropertyExpr* expr) -> VisitResult override {
    return Walk(expr->GetValue());
  }

  auto VisitWildcardPattern(expr::WildcardPatternExpr*) -> VisitResult override {
    return true;
  }

  auto VisitLiteralPattern(expr::LiteralPatternExpr*) -> VisitResult override {
    return true;
  }

  auto VisitBindingPattern(expr::BindingPatternExpr* expr) -> VisitResult override {
    defs.insert(expr->GetLocal());
    return true;
  }

  auto VisitVariantPattern(expr::VariantPatternExpr* expr) -> VisitResult override {
    return Walk(expr->GetPayload());
  }

  // A nested lambda's own free locals are free in this one too, unless this
  // lambda binds them.
  auto VisitLambda(expr::LambdaExpr* expr) -> VisitResult override {
    FreeLocals inner{};
    for (const auto param : expr->GetParams())
      inner.defs.insert(param);

    if (!inner.Walk(expr->GetBody()))
      return false;

    for (const auto local : inner.order) {
      if (!inner.defs.contains(local))
        Use(local);
    }

    return true;
  }

  auto VisitFold(expr::FoldExpr* expr) -> VisitResult override {
    defs.insert(expr->GetBinding());
    defs.insert(expr->GetAccumulator());
    return Walk(expr->GetIterable()) && Walk(expr->GetInitial()) && Walk(expr->GetClause()) && Walk(expr->GetBody());
  }

  auto VisitVariant(expr::VariantExpr* expr) -> VisitResult override {
    return Walk(expr->GetPayload());
  }

  auto VisitCall(expr::CallExpr* expr) -> VisitResult override {
    if (!Walk(expr->GetTarget()))
      return false;

    for (const auto arg : expr->GetArgs()) {
      if (!Walk(arg))
        return false;
    }

    return true;
  }
};
}  // namespace

auto FlowGraphBuilder::BuildLambda(Context* ctx, expr::LambdaExpr* lambda, const std::vector<LocalVariable*>& captures)
    -> FlowGraph* {
  const auto name = "lambda." + std::to_string(ctx->lambda_count++);
  FlowGraphBuilder builder(ctx, name, nullptr);
  builder.graph_->is_lambda_ = true;
  builder.AddParameters(lambda->GetParams(), captures);
  if (!builder.BuildBody(lambda->GetBody()))
    return nullptr;

  const auto graph = builder.Finish();
  ctx->graph->Add(graph);
  return ctx->errors.empty() ? graph : nullptr;
}

auto FlowGraphBuilder::BuildModuleGraph(Module* module, std::vector<std::string>* errors) -> ModuleGraph* {
  Context ctx{};
  ctx.graph = new ModuleGraph();
  for (const auto func : module->functions)
    ctx.functions[std::string(func->GetName()->GetData())] = func;

  const auto report = [&]() -> ModuleGraph* {
    if (errors)
      errors->insert(errors->end(), ctx.errors.begin(), ctx.errors.end());

    return nullptr;
  };

  {
    FlowGraphBuilder builder(&ctx, "init", nullptr);
    if (!builder.BuildInit(module->bindings))
      return report();

    ctx.graph->SetInit(builder.Finish());
  }

  for (const auto func : module->functions) {
    if (func->IsExtern()) {
      ctx.graph->Add(FlowGraph::New(std::string(func->GetName()->GetData()), func, nullptr));
      continue;
    }
    if (!func->HasBody())
      continue;

    FlowGraphBuilder builder(&ctx, std::string(func->GetName()->GetData()), func);
    std::vector<LocalVariable*> params{};
    for (const auto& param : func->GetParameters())
      params.push_back(param.local);

    builder.AddParameters(params, {});
    if (!builder.BuildBody(func->GetBody()))
      return report();

    ctx.graph->Add(builder.Finish());
  }

  if (!ctx.errors.empty())
    return report();

  return ctx.graph;
}

auto FlowGraphBuilder::BuildFlowGraph(expr::SeqExpr* expr) -> FlowGraph* {
  if (!expr)
    return nullptr;

  Context ctx{};
  ctx.graph = new ModuleGraph();
  FlowGraphBuilder builder(&ctx, "anonymous", nullptr);
  if (!builder.BuildBody(expr)) {
    for (const auto& error : ctx.errors)
      std::cerr << error << std::endl;

    return nullptr;
  }

  const auto graph = builder.Finish();
  for (const auto& error : ctx.errors)
    std::cerr << error << std::endl;

  return ctx.errors.empty() ? graph : nullptr;
}

// Used by the lambda lowering in the visitors.
auto CollectCaptures(expr::LambdaExpr* lambda) -> std::vector<LocalVariable*> {
  FreeLocals free{};
  for (const auto param : lambda->GetParams())
    free.defs.insert(param);

  free.Walk(lambda->GetBody());
  std::vector<LocalVariable*> captures{};
  for (const auto local : free.order) {
    if (free.defs.contains(local) || FlowGraphBuilder::IsGlobal(local))
      continue;

    captures.push_back(local);
  }

  return captures;
}
}  // namespace kura
