#include "backend/type_analysis.h"
#include "c_type.h"

#include <algorithm>
#include <functional>
#include <map>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "backend/ir.h"
#include "frontend/expr.h"
#include "frontend/type_expr.h"
#include "function.h"
#include "local_scope.h"
#include "module.h"
#include "object.h"
#include "property.h"

namespace kura {
namespace {
using Params = std::unordered_map<std::string, CompileType*>;

class Analyzer {
 private:
  struct Signature {
    std::vector<CompileType*> params{};
    CompileType* declared = nullptr;  // null: inferred
    CompileType* result = nullptr;
  };

  // The graph being analyzed.
  struct Context {
    FlowGraph* graph;
    std::unordered_map<Definition*, std::vector<Instruction*>> users{};
    bool report = false;
    CompileType* result = nullptr;
    // blocks no execution can reach, given what the variant tests rule out
    std::unordered_set<EntryInstr*> dead{};
  };

  struct LambdaState {
    bool reported = false;
    CompileType* result = nullptr;
  };

  Module* module_;
  ModuleGraph* graphs_;
  std::vector<std::string>* errors_;
  bool failed_ = false;

  std::unordered_map<std::string, expr::TypeDecl*> decls_{};
  std::unordered_set<std::string> resolving_{};
  std::unordered_map<std::string, CompileType*> declared_variants_{};
  std::unordered_map<LocalVariable*, expr::TypeExpr*> annotations_{};
  std::unordered_map<LocalVariable*, CompileType*> globals_{};

  std::unordered_map<Function*, Signature> signatures_{};
  std::unordered_set<Function*> analyzed_{};
  std::unordered_set<Function*> in_progress_{};
  std::unordered_map<FlowGraph*, LambdaState> lambdas_{};
  std::unordered_set<NewClosureInstr*> analyzed_closures_{};

  std::vector<Context*> stack_{};

  // ── diagnostics ──────────────────────────────────────────────────────────
  void Error(const std::string& message) {
    failed_ = true;
    if (errors_)
      errors_->push_back(message);
  }

  // Reported once, while the types are stable.
  void Report(Instruction* instr, const std::string& message) {
    if (!stack_.empty() && !stack_.back()->report)
      return;
    std::stringstream ss{};
    if (!stack_.empty())
      ss << stack_.back()->graph->GetName() << ": ";
    if (instr) {
      if (instr->IsDefinition())
        ss << "%" << instr->GetId() << " = ";
      ss << instr->ToString() << ": ";
    }
    ss << message;
    Error(ss.str());
  }

  // ── type expressions ─────────────────────────────────────────────────────
  auto Resolve(expr::TypeExpr* te, const Params& params, const std::string& where, const bool in_union = false)
      -> CompileType* {
    if (!te)
      return CompileType::Any();

    if (const auto named = te->AsNamed()) {
      const auto& name = named->GetTypeName();
      const auto& args = named->GetArgs();
      if (args.empty()) {
        if (const auto it = params.find(name); it != params.end())
          return it->second;
      }

      if (name.starts_with("c.")) {
        if (const auto c = CType::FromName(name))
          return c->ToKuraType();
      }

      const auto simple = [&](CompileType* type) -> CompileType* {
        if (!args.empty())
          Error(where + ": type `" + name + "` takes no type arguments");
        return type;
      };
      if (name == "Bool")
        return simple(CompileType::Bool());
      if (name == "None")
        return simple(CompileType::None());
      if (name == "Int")
        return simple(CompileType::Int());
      if (name == "Float")
        return simple(CompileType::Float());
      if (name == "Number")
        return simple(CompileType::Number());
      if (name == "String")
        return simple(CompileType::String());
      if (name == "Color")
        return simple(CompileType::Color());
      if (name == "Px")
        return simple(CompileType::Px());
      if (name == "Pct")
        return simple(CompileType::Pct());
      if (name == "Fr")
        return simple(CompileType::Fr());
      if (name == "Node")
        return simple(CompileType::Node());
      if (name == "Seq" || name == "Optional") {
        if (args.size() != 1) {
          Error(where + ": `" + name + "` takes one type argument");
          return CompileType::Any();
        }
        const auto arg = Resolve(args[0], params, where);
        // `Optional<T>` is `T | None`; there's no wrapper value
        return name == "Seq" ? CompileType::Seq(arg) : CompileType::Union({arg, CompileType::None()});
      }

      if (const auto decl = decls_.find(name); decl != decls_.end()) {
        const auto& formal = decl->second->GetParams();
        if (formal.size() != args.size()) {
          Error(where + ": type `" + name + "` takes " + std::to_string(formal.size()) + " type argument(s)");
          return CompileType::Any();
        }
        if (!resolving_.insert(name).second) {
          Error(where + ": type `" + name + "` is recursive, which isn't supported yet");
          return CompileType::Any();
        }
        Params inner{};
        for (size_t idx = 0; idx < formal.size(); idx++)
          inner[formal[idx]] = Resolve(args[idx], params, where);
        const auto result = Resolve(decl->second->GetDefinition(), inner, "type " + name);
        resolving_.erase(name);
        return result;
      }

      // a name nothing declares, inside a union, is a constructor without a payload
      if (in_union && args.empty())
        return CompileType::Variant(name);
      Error(where + ": unknown type `" + name + "`");
      return CompileType::Any();
    }

    if (const auto variant = te->AsVariant())
      return CompileType::Variant(variant->GetTypeName(), Resolve(variant->GetPayload(), params, where));

    if (const auto record = te->AsRecord()) {
      std::vector<CompileType::Field> fields{};
      for (const auto& field : record->GetFields())
        fields.push_back({field.name, Resolve(field.type, params, where), field.optional});
      return CompileType::Record(fields);
    }

    if (const auto function = te->AsFunction()) {
      std::vector<CompileType*> parameters{};
      for (const auto param : function->GetParams())
        parameters.push_back(Resolve(param, params, where));
      return CompileType::Function(parameters, Resolve(function->GetResult(), params, where));
    }

    if (const auto onion = te->AsUnion()) {
      std::vector<CompileType*> members{};
      for (const auto member : onion->GetMembers())
        members.push_back(Resolve(member, params, where, true));
      return CompileType::Union(members);
    }
    return CompileType::Any();
  }

  void CollectVariants(CompileType* type) {
    for (const auto alt : type->GetAlternatives()) {
      if (alt->IsVariant())
        declared_variants_[alt->GetVariantName()] = alt;
    }
  }

  // ── signatures ───────────────────────────────────────────────────────────
  auto GetSignature(Function* function) -> Signature& {
    auto [it, inserted] = signatures_.try_emplace(function);
    auto& sig = it->second;
    if (!inserted)
      return sig;
    const std::string name(function->GetName()->GetData());
    if (const auto c = function->IsExtern() ? function->GetCSignature() : nullptr) {
      sig.params = c->KuraParams();
      sig.declared = sig.result = c->KuraResult();
      return sig;
    }
    for (const auto& param : function->GetParameters())
      sig.params.push_back(Resolve(param.type, {}, "fn " + name));
    if (function->GetReturnType()) {
      sig.declared = Resolve(function->GetReturnType(), {}, "fn " + name);
      sig.result = sig.declared;
    }
    return sig;
  }

  auto FunctionType(Function* function) -> CompileType* {
    EnsureAnalyzed(function);
    const auto& sig = GetSignature(function);
    return CompileType::Function(sig.params, sig.result ? sig.result : CompileType::Any());
  }

  void EnsureAnalyzed(Function* function) {
    const auto graph = graphs_->GetGraphFor(function);
    if (!graph || analyzed_.contains(function))
      return;
    auto& sig = GetSignature(function);
    if (function->IsExtern()) {
      analyzed_.insert(function);
      graph->SetSignature(sig.params, sig.result);
      return;
    }
    // a declared return type is all a caller needs, even while the body is
    // being analyzed (recursion)
    if (in_progress_.contains(function)) {
      if (!sig.declared)
        Error("fn " + std::string(function->GetName()->GetData()) +
              ": recursion needs a declared return type, or the result can't be inferred");
      return;
    }
    in_progress_.insert(function);
    const auto inferred = Analyze(graph, sig.params, true);
    in_progress_.erase(function);
    analyzed_.insert(function);

    if (sig.declared) {
      if (!CompileType::IsAssignable(inferred, sig.declared))
        Error("fn " + std::string(function->GetName()->GetData()) + ": returns " + inferred->ToString() +
              " but is declared to return " + sig.declared->ToString());
    } else {
      sig.result = inferred;
    }
    graph->SetSignature(sig.params, sig.result);
  }

  // Which variants a subject can still be on entry to each block. A match tests
  // its subject against each constructor in turn; once every constructor has
  // failed to match, the code after the last arm can't run. Subjects of type
  // Any, and tests other than variant tests, rule nothing out.
  using Remaining = std::unordered_map<Definition*, std::vector<CompileType*>>;

  auto ComputeDeadBlocks(Context& ctx) -> std::unordered_set<EntryInstr*> {
    std::unordered_set<EntryInstr*> dead{};
    struct State {
      bool reachable = false;
      Remaining remaining{};
    };
    std::unordered_map<EntryInstr*, State> states{};

    const auto subject_alternatives = [&](Definition* subject, const Remaining& remaining) {
      const auto it = remaining.find(subject);
      if (it != remaining.end())
        return it->second;
      return subject->HasCompileType() ? subject->GetCompileType()->GetAlternatives()
                                       : std::vector<CompileType*>{};
    };

    for (const auto block : ctx.graph->GetReversePostorder()) {
      auto& state = states[block];
      if (block->IsGraphEntry()) {
        state.reachable = true;
        continue;
      }

      bool first = true;
      Remaining merged{};
      for (uint64_t idx = 0; idx < block->GetNumberOfPredecessors(); idx++) {
        const auto pred = block->GetPredecessorAt(idx);
        const auto found = states.find(pred);
        // a back edge: nothing is known about it yet, so assume it can run
        const auto processed = found != states.end();
        if (processed && !found->second.reachable)
          continue;

        Remaining edge = processed ? found->second.remaining : Remaining{};
        bool feasible = true;
        if (processed && !pred->IsGraphEntry()) {
          const auto branch = FlowGraph::GetTerminator(pred)->AsBranch();
          const auto cond = branch && branch->GetCondition() ? branch->GetCondition()->GetDefinition() : nullptr;

          // the tests a match lowers to, as the alternatives of the subject
          // they accept: a variant by name, `Some` (anything but None), and
          // the `None` literal
          Definition* subject = nullptr;
          std::function<bool(CompileType*)> accepts{};
          if (const auto test = cond ? cond->AsTestVariant() : nullptr) {
            subject = test->GetValue()->GetDefinition();
            if (test->GetVariantName() == "Some") {
              accepts = [](CompileType* alt) { return !alt->IsNone(); };
            } else {
              const auto name = test->GetVariantName();
              accepts = [name](CompileType* alt) { return alt->IsVariant() && alt->GetVariantName() == name; };
            }
          } else if (const auto eq = cond ? cond->AsBinaryOp() : nullptr; eq && eq->GetOp() == BinaryOp::kEq) {
            const auto is_none = [](Value* v) {
              const auto c = v && v->GetDefinition() ? v->GetDefinition()->AsConstant() : nullptr;
              return c && c->GetValue()->GetType() == kNoneType;
            };
            if (is_none(eq->GetRight()))
              subject = eq->GetLeft()->GetDefinition();
            else if (is_none(eq->GetLeft()))
              subject = eq->GetRight()->GetDefinition();
            accepts = [](CompileType* alt) { return alt->IsNone(); };
          }

          const auto type = subject && subject->HasCompileType() ? subject->GetCompileType() : nullptr;
          if (type && !type->IsAny() && !type->IsNever() && branch->GetThen() != branch->GetElse()) {
            auto alts = subject_alternatives(subject, edge);
            if (block == branch->GetThen())
              std::erase_if(alts, [&](CompileType* alt) { return !accepts(alt); });
            else
              std::erase_if(alts, [&](CompileType* alt) { return accepts(alt); });
            feasible = !alts.empty();
            edge[subject] = alts;
          }
        }
        if (!feasible)
          continue;

        state.reachable = true;
        if (first) {
          merged = edge;
          first = false;
          continue;
        }
        // possible along any incoming edge; a subject an edge says nothing
        // about could be anything
        Remaining joined{};
        for (auto& [subject, alts] : merged) {
          const auto other = edge.find(subject);
          if (other == edge.end())
            continue;
          auto all = alts;
          all.insert(all.end(), other->second.begin(), other->second.end());
          std::sort(all.begin(), all.end());
          all.erase(std::unique(all.begin(), all.end()), all.end());
          joined[subject] = all;
        }
        merged = std::move(joined);
      }
      state.remaining = std::move(merged);
      if (!state.reachable)
        dead.insert(block);
    }
    return dead;
  }

  // ── graph analysis ───────────────────────────────────────────────────────
  // Returns the type of the graph's result.
  auto Analyze(FlowGraph* graph, const std::vector<CompileType*>& params, const bool report) -> CompileType* {
    Context ctx{};
    ctx.graph = graph;
    ctx.report = false;
    for (const auto block : graph->GetReversePostorder()) {
      for (Instruction* instr = block; instr != nullptr; instr = instr->GetNext()) {
        for (uint64_t idx = 0; idx < instr->GetNumberOfInputs(); idx++) {
          if (const auto input = instr->GetInputAt(idx); input && input->GetDefinition())
            ctx.users[input->GetDefinition()].push_back(instr);
        }
        if (const auto phi = instr->AsPhi()) {
          for (const auto& in : phi->GetIncoming()) {
            if (in.value && in.value->GetDefinition())
              ctx.users[in.value->GetDefinition()].push_back(instr);
          }
        }
      }
    }

    stack_.push_back(&ctx);
    // Within a round the types settle (the cap guards against a bug). What they
    // say about which blocks are dead can shrink them afterwards, so rounds
    // repeat, from scratch, until the dead set is stable.
    for (auto round = 0; round < 8; round++) {
      for (const auto block : graph->GetReversePostorder()) {
        for (Instruction* instr = block; instr != nullptr; instr = instr->GetNext()) {
          if (instr->IsDefinition())
            static_cast<Definition*>(instr)->SetCompileType(nullptr);
        }
      }
      for (auto iteration = 0; iteration < 64; iteration++) {
        ctx.result = nullptr;
        bool changed = false;
        for (const auto block : graph->GetReversePostorder()) {
          if (ctx.dead.contains(block))
            continue;
          for (Instruction* instr = block; instr != nullptr; instr = instr->GetNext())
            changed |= Transfer(instr, params);
        }
        if (!changed)
          break;
      }
      auto dead = ComputeDeadBlocks(ctx);
      if (dead == ctx.dead)
        break;
      ctx.dead = std::move(dead);
    }
    graph->SetDeadBlocks(ctx.dead);
    if (report) {
      ctx.report = true;
      ctx.result = nullptr;
      for (const auto block : graph->GetReversePostorder()) {
        if (ctx.dead.contains(block))
          continue;
        for (Instruction* instr = block; instr != nullptr; instr = instr->GetNext())
          Transfer(instr, params);
      }
      // closures nothing called or passed on still get checked
      for (const auto block : graph->GetReversePostorder()) {
        if (ctx.dead.contains(block))
          continue;
        for (Instruction* instr = block; instr != nullptr; instr = instr->GetNext()) {
          if (const auto closure = instr->AsNewClosure(); closure && !analyzed_closures_.contains(closure))
            AnalyzeClosure(closure, std::vector<CompileType*>(ClosureArity(closure), CompileType::Any()));
        }
      }
    }
    stack_.pop_back();
    return ctx.result ? ctx.result : CompileType::Never();
  }

  static auto ClosureArity(NewClosureInstr* closure) -> uint64_t {
    return closure->GetTarget()->GetNumberOfParameters() - closure->GetTarget()->GetCaptures().size();
  }

  // Analyzes the lambda of `closure` with `params` for its declared
  // parameters, and returns its function type.
  auto AnalyzeClosure(NewClosureInstr* closure, std::vector<CompileType*> params) -> CompileType* {
    const auto graph = closure->GetTarget();
    const auto report = !stack_.empty() && stack_.back()->report;
    auto& state = lambdas_[graph];
    if (!(report && state.reported)) {
      std::vector<CompileType*> all = params;
      for (const auto capture : closure->GetCaptures())
        all.push_back(TypeOf(capture));
      state.result = Analyze(graph, all, report);
      state.reported = report;
      graph->SetSignature(all, state.result);
    }
    if (report)
      analyzed_closures_.insert(closure);
    const auto type = CompileType::Function(params, state.result ? state.result : CompileType::Never());
    Set(closure, type);
    return type;
  }

  // A lambda whose final analysis hasn't happened: the call it's passed to (or
  // that calls it) decides its parameter types.
  auto IsPendingClosure(Value* value) -> NewClosureInstr* {
    if (!value || !value->GetDefinition())
      return nullptr;
    const auto closure = value->GetDefinition()->AsNewClosure();
    return closure && !analyzed_closures_.contains(closure) ? closure : nullptr;
  }

  auto TypeOf(Value* value) -> CompileType* {
    if (!value || !value->GetDefinition())
      return CompileType::Never();
    const auto def = value->GetDefinition();
    if (const auto closure = def->AsNewClosure(); closure && !closure->HasCompileType())
      return AnalyzeClosure(closure, std::vector<CompileType*>(ClosureArity(closure), CompileType::Any()));
    return def->HasCompileType() ? def->GetCompileType() : CompileType::Never();
  }

  // A definition's type is a function of its inputs' types, so each iteration
  // replaces it. (Joining with the previous iteration's type would keep a stale
  // answer: a record whose spread source wasn't typed yet is `{ x: Never }`, which
  // doesn't join into the right record type.) The iteration where nothing changes
  // is a fixpoint. Returns whether the type changed.
  auto Set(Definition* def, CompileType* type) -> bool {
    const auto old = def->GetCompileType();
    def->SetCompileType(type);
    return type != old;
  }

  // ── operators ────────────────────────────────────────────────────────────
  static auto Mixed(CompileType* a, CompileType* b) -> std::string {
    return "can't mix " + a->ToString() + " and " + b->ToString() + "; convert one of them explicitly";
  }

  // Int op Int is Int and Float op Float is Float. `Number` is one of the two,
  // so combining it with either gives a Number.
  auto Arithmetic(BinaryOp op, CompileType* a, CompileType* b, Instruction* at) -> CompileType* {
    if (a->IsNumeric() && b->IsNumeric()) {
      if (a == b)
        return a;
      if (a->IsNumber() || b->IsNumber())
        return CompileType::Number();
      Report(at, Mixed(a, b));
      return CompileType::Any();
    }
    if (a->IsUnit() || b->IsUnit()) {
      if (a == b) {
        switch (op) {
          case BinaryOp::kAdd:
          case BinaryOp::kSubtract:
          case BinaryOp::kModulus:
            return a;
          case BinaryOp::kDivide:
            return CompileType::Float();  // Px / Px: a ratio
          default:
            break;
        }
      } else if (a->IsUnit() && b->IsNumeric() && (op == BinaryOp::kMultiply || op == BinaryOp::kDivide)) {
        return a;  // scaling
      } else if (a->IsNumeric() && b->IsUnit() && op == BinaryOp::kMultiply) {
        return b;
      }
    }
    Report(at, std::string("can't apply `") + std::string(ToString(op)) + "` to " + a->ToString() + " and " +
                   b->ToString());
    return CompileType::Any();
  }

  // Could a value of `a` ever equal a value of `b`? Two constructors of a sum
  // type can be compared (they're just never equal), and unions compare if any
  // alternative of one can equal any alternative of the other.
  auto Comparable(CompileType* a, CompileType* b) -> bool {
    if (a->IsAny() || b->IsAny())
      return true;
    if (a->IsUnion() || b->IsUnion()) {
      for (const auto x : a->GetAlternatives()) {
        for (const auto y : b->GetAlternatives()) {
          if (Comparable(x, y))
            return true;
        }
      }
      return false;
    }
    if (a->IsVariant() && b->IsVariant())
      return true;
    if (a->IsNumeric() && b->IsNumeric())
      return a == b || a->IsNumber() || b->IsNumber();
    return CompileType::IsAssignable(a, b) || CompileType::IsAssignable(b, a);
  }

  auto BinaryResult(BinaryOpInstr* instr) -> CompileType* {
    const auto a = TypeOf(instr->GetLeft());
    const auto b = TypeOf(instr->GetRight());
    if (a->IsNever() || b->IsNever())
      return CompileType::Never();
    const auto op = instr->GetOp();
    switch (op) {
      case BinaryOp::kEq:
      case BinaryOp::kNeq:
        if (!Comparable(a, b))
          Report(instr, "can't compare " + a->ToString() + " with " + b->ToString());
        return CompileType::Bool();
      case BinaryOp::kLessThan:
      case BinaryOp::kLessThanEqual:
      case BinaryOp::kGreaterThan:
      case BinaryOp::kGreaterThanEqual:
        if (a->IsAny() || b->IsAny())
          return CompileType::Bool();
        if (a->IsString() && b->IsString())
          return CompileType::Bool();
        if ((a->IsNumeric() && b->IsNumeric()) || (a->IsUnit() && a == b)) {
          Arithmetic(BinaryOp::kAdd, a, b, instr);  // reports an Int/Float mix
          return CompileType::Bool();
        }
        Report(instr, "can't order " + a->ToString() + " and " + b->ToString());
        return CompileType::Bool();
      case BinaryOp::kBinaryAnd:
      case BinaryOp::kBinaryOr:
        if (!(a->IsBool() || a->IsAny()) || !(b->IsBool() || b->IsAny()))
          Report(instr, "`&&` and `||` need Bool operands, not " + a->ToString() + " and " + b->ToString());
        return CompileType::Bool();
      case BinaryOp::kAdd:
        if (a->IsAny() || b->IsAny())
          return CompileType::Any();
        if (a->IsString() && b->IsString())
          return CompileType::String();
        if (a->IsSeq() && b->IsSeq())
          return CompileType::Seq(CompileType::Join(a->GetElementType(), b->GetElementType()));
        if (a->IsRecord() && b->IsRecord()) {
          std::map<std::string, CompileType::Field> merged{};
          for (const auto& f : a->GetFields())
            merged[f.name] = f;
          for (const auto& f : b->GetFields())
            merged[f.name] = f;
          std::vector<CompileType::Field> fields{};
          for (const auto& [name, field] : merged)
            fields.push_back(field);
          return CompileType::Record(fields);
        }
        return Arithmetic(op, a, b, instr);
      default:
        if (a->IsAny() || b->IsAny())
          return CompileType::Any();
        return Arithmetic(op, a, b, instr);
    }
  }

  auto UnaryResult(UnaryOpInstr* instr) -> CompileType* {
    const auto a = TypeOf(instr->GetValue());
    if (a->IsNever() || a->IsAny())
      return a;
    switch (instr->GetOp()) {
      case UnaryOp::kBang:
        if (!a->IsBool())
          Report(instr, "`!` needs a Bool, not " + a->ToString());
        return CompileType::Bool();
      default:
        if (a->IsNumeric() || a->IsUnit())
          return a;
        Report(instr, "can't negate " + a->ToString());
        return CompileType::Any();
    }
  }

  auto ConstantType(Object* value) -> CompileType* {
    switch (value->GetType()) {
      case kNoneType:
        return CompileType::None();
      case kBoolType:
        return CompileType::Bool();
      case kIntType:
        return CompileType::Int();
      case kFloatType:
        return CompileType::Float();
      case kNumberType:
        return CompileType::Number();
      case kStringType:
        return CompileType::String();
      case kColorType:
        return CompileType::Color();
      case kPxType:
        return CompileType::Px();
      case kPctType:
        return CompileType::Pct();
      case kFrType:
        return CompileType::Fr();
      default:
        return CompileType::Any();
    }
  }

  // ── instructions ─────────────────────────────────────────────────────────
  auto PropertyName(Property* property) -> std::string {
    return std::string(property->GetPropertyName()->GetData());
  }

  auto LoadPropertyType(Instruction* at, Value* instance, Property* property, const bool safe) -> CompileType* {
    auto type = TypeOf(instance);
    if (type->IsNever())
      return type;
    if (type->IsAny() || type->IsNode())
      return CompileType::Any();
    const auto name = PropertyName(property);
    const auto nullable = type->IsNullable();
    if (nullable) {
      if (!safe)
        Report(at, "`" + type->ToString() + "` may be None; use `?.` to access `" + name + "`");
      type = type->WithoutNone();
    }
    if (!type->IsRecord()) {
      Report(at, "can't read property `" + name + "` of " + type->ToString());
      return CompileType::Any();
    }
    const auto field = type->FindField(name);
    if (!field) {
      Report(at, type->ToString() + " has no property `" + name + "`");
      return CompileType::Any();
    }
    auto result = field->type;
    if (field->optional || (safe && nullable))
      result = CompileType::Union({result, CompileType::None()});
    return result;
  }

  auto RecordType(NewRecordInstr* record, Context& ctx) -> CompileType* {
    std::map<std::string, CompileType::Field> fields{};
    const auto it = ctx.users.find(record);
    if (it != ctx.users.end()) {
      for (const auto user : it->second) {
        if (const auto store = user->AsStoreProperty()) {
          if (store->GetInstance()->GetDefinition() != record)
            continue;
          const auto name = PropertyName(store->GetProperty());
          fields[name] = {name, TypeOf(store->GetValue()), false};
        } else if (const auto spread = user->AsSpread()) {
          if (spread->GetInstance()->GetDefinition() != record)
            continue;
          const auto source = TypeOf(spread->GetValue());
          if (source->IsNever())
            continue;
          if (source->IsAny())
            return CompileType::Any();
          if (!source->IsRecord()) {
            Report(spread, "can only spread a record, not " + source->ToString());
            return CompileType::Any();
          }
          for (const auto& field : source->GetFields())
            fields[field.name] = field;
        }
      }
    }
    std::vector<CompileType::Field> list{};
    for (const auto& [name, field] : fields)
      list.push_back(field);
    return CompileType::Record(list);
  }

  auto ListType(NewListInstr* list, Context& ctx) -> CompileType* {
    auto element = CompileType::Never();
    for (const auto input : list->GetInputs())
      element = CompileType::Join(element, TypeOf(input));
    // a comprehension builds its list by storing each element
    if (const auto it = ctx.users.find(list); it != ctx.users.end()) {
      for (const auto user : it->second) {
        if (const auto store = user->AsStoreIndex(); store && store->GetInstance()->GetDefinition() == list)
          element = CompileType::Join(element, TypeOf(store->GetValue()));
      }
    }
    return CompileType::Seq(element);
  }

  auto CallResult(CallInstr* call) -> CompileType* {
    const auto target_value = call->GetTarget();
    const auto& args = call->GetArgs();

    // `((x) => x + 1)(2)`: the arguments give the lambda's parameter types
    if (const auto closure = IsPendingClosure(target_value)) {
      std::vector<CompileType*> params{};
      for (const auto arg : args)
        params.push_back(IsPendingClosure(arg) ? CompileType::Any() : TypeOf(arg));
      if (params.size() == ClosureArity(closure))
        AnalyzeClosure(closure, params);
    }

    const auto target = TypeOf(target_value);
    if (target->IsNever())
      return target;
    if (target->IsAny()) {
      for (const auto arg : args)
        TypeOf(arg);
      return CompileType::Any();
    }
    if (!target->IsFunction()) {
      Report(call, "can't call " + target->ToString());
      return CompileType::Any();
    }
    const auto& params = target->GetParameters();
    const auto direct = target_value->GetDefinition()->AsLoadFunction();
    // a callback is handed to C as the address of a function, so it has to be one
    if (direct && direct->GetFunction()->IsExtern()) {
      size_t position = 0;
      for (const auto& param : direct->GetFunction()->GetCSignature()->params) {
        if (param.kind == CType::kOut)
          continue;
        if (param.kind == CType::kCallback && position < args.size()) {
          const auto given = args[position]->GetDefinition()->AsLoadFunction();
          if (!given || given->GetFunction()->IsExtern())
            Report(call, "argument " + std::to_string(position + 1) +
                             ": a callback must be the name of a function written in Kura, not a closure or a value");
        }
        position++;
      }
    }
    // a variadic C function takes any number of arguments past its fixed ones
    const auto variadic = direct && direct->GetFunction()->IsExtern() && direct->GetFunction()->GetCSignature()->variadic;
    if (variadic ? args.size() < params.size() : params.size() != args.size()) {
      Report(call, std::string("expected ") + (variadic ? "at least " : "") + std::to_string(params.size()) +
                       " argument(s) but got " + std::to_string(args.size()));
      return target->GetResult();
    }
    for (size_t idx = 0; idx < args.size(); idx++) {
      if (idx >= params.size()) {
        const auto extra = TypeOf(args[idx]);
        if (!(extra->IsInt() || extra->IsFloat() || extra->IsBool() || extra->IsString() || extra->IsHandle()))
          Report(call, "argument " + std::to_string(idx + 1) + " is " + extra->ToString() +
                           ", which can't be passed to a variadic C function (use an Int, Float, Bool, String or handle)");
        continue;
      }
      // a lambda argument is typed from the parameter it's passed for
      if (const auto closure = IsPendingClosure(args[idx])) {
        std::vector<CompileType*> expected(ClosureArity(closure), CompileType::Any());
        if (params[idx]->IsFunction() && params[idx]->GetParameters().size() == expected.size())
          expected = params[idx]->GetParameters();
        AnalyzeClosure(closure, expected);
      }
      const auto arg = TypeOf(args[idx]);
      if (!CompileType::IsAssignable(arg, params[idx]))
        Report(call, "argument " + std::to_string(idx + 1) + " is " + arg->ToString() + " but " +
                         params[idx]->ToString() + " is expected");
    }
    return target->GetResult();
  }

  auto Transfer(Instruction* instr, const std::vector<CompileType*>& params) -> bool {
    auto& ctx = *stack_.back();

    if (const auto branch = instr->AsBranch()) {
      const auto condition = TypeOf(branch->GetCondition());
      if (!(condition->IsBool() || condition->IsAny() || condition->IsNever()))
        Report(instr, "the condition is " + condition->ToString() + ", not Bool");
      return false;
    }
    if (const auto ret = instr->AsReturn()) {
      const auto type = TypeOf(ret->GetValue());
      ctx.result = ctx.result ? CompileType::Join(ctx.result, type) : type;
      return Set(ret, type);
    }
    if (const auto store = instr->AsStoreLocal()) {
      const auto type = TypeOf(store->GetValue());
      auto declared = type;
      if (const auto it = annotations_.find(store->GetLocal()); it != annotations_.end()) {
        declared = Resolve(it->second, {}, "let " + std::string(store->GetLocal()->GetName()->GetData()));
        if (!CompileType::IsAssignable(type, declared))
          Report(instr, "`" + std::string(store->GetLocal()->GetName()->GetData()) + "` is declared " +
                            declared->ToString() + " but its value is " + type->ToString());
      }
      const auto before = globals_.find(store->GetLocal());
      const auto changed = before == globals_.end() || before->second != declared;
      globals_[store->GetLocal()] = declared;
      return changed;
    }
    if (const auto store = instr->AsStoreIndex()) {
      const auto type = TypeOf(store->GetInstance());
      if (!(type->IsSeq() || type->IsNode() || type->IsAny() || type->IsNever()))
        Report(instr, "can't store into " + type->ToString());
      return false;
    }
    if (instr->IsStoreProperty() || instr->IsSpread() || instr->IsGoto() || instr->IsLabel() || instr->IsGraphEntry() ||
        instr->IsTargetEntry() || instr->IsJoinEntry())
      return false;

    const auto def = instr->IsDefinition() ? static_cast<Definition*>(instr) : nullptr;
    if (!def)
      return false;

    if (const auto c = instr->AsConstant())
      return Set(def, ConstantType(c->GetValue()));
    if (const auto p = instr->AsParameter())
      return Set(def, p->GetIndex() < params.size() ? params[p->GetIndex()] : CompileType::Any());
    if (const auto f = instr->AsLoadFunction())
      return Set(def, FunctionType(f->GetFunction()));
    if (const auto l = instr->AsLoadLocal()) {
      const auto it = globals_.find(l->GetLocal());
      return Set(def, it == globals_.end() ? CompileType::Any() : it->second);
    }
    if (const auto b = instr->AsBinaryOp())
      return Set(def, BinaryResult(b));
    if (const auto u = instr->AsUnaryOp())
      return Set(def, UnaryResult(u));
    if (const auto r = instr->AsNewRecord())
      return Set(def, RecordType(r, ctx));
    if (const auto l = instr->AsNewList())
      return Set(def, ListType(l, ctx));
    if (instr->IsNewNode())
      return Set(def, CompileType::Node());
    if (const auto l = instr->AsLoadProperty())
      return Set(def, LoadPropertyType(instr, l->GetInstance(), l->GetProperty(), false));
    if (const auto l = instr->AsLoadPropertySafely())
      return Set(def, LoadPropertyType(instr, l->GetInstance(), l->GetProperty(), true));
    if (const auto l = instr->AsLoadIndex()) {
      const auto type = TypeOf(l->GetInstance());
      const auto index = TypeOf(l->GetIndex());
      if (!(index->IsInt() || index->IsNumber() || index->IsAny() || index->IsNever()))
        Report(instr, "an index must be an Int, not " + index->ToString());
      if (type->IsSeq())
        return Set(def, type->GetElementType());
      if (!(type->IsAny() || type->IsNode() || type->IsNever()))
        Report(instr, "can't index " + type->ToString());
      return Set(def, type->IsNever() ? type : CompileType::Any());
    }
    if (const auto l = instr->AsLoadLength()) {
      const auto type = TypeOf(l->GetInstance());
      if (!(type->IsSeq() || type->IsNode() || type->IsAny() || type->IsNever()))
        Report(instr, "can't iterate over " + type->ToString());
      return Set(def, CompileType::Int());
    }
    if (const auto v = instr->AsNewVariant()) {
      const auto payload = v->HasPayload() ? TypeOf(v->GetPayload()) : nullptr;
      if (v->GetVariantName() == "Some") {
        if (!payload) {
          Report(instr, "`Some` needs a value");
          return Set(def, CompileType::Any());
        }
        return Set(def, payload);  // `Some(x)` is just x: Optional<T> is T | None
      }
      const auto declared = declared_variants_.find(v->GetVariantName());
      if (declared == declared_variants_.end()) {
        Report(instr, "unknown variant `" + v->GetVariantName() + "`");
        return Set(def, CompileType::Any());
      }
      const auto expected = declared->second->GetPayload();
      if (!expected != !payload) {
        Report(instr, expected ? "`" + v->GetVariantName() + "` needs a payload"
                               : "`" + v->GetVariantName() + "` takes no payload");
      } else if (expected && !payload->IsNever() && !CompileType::IsAssignable(payload, expected)) {
        Report(instr, "`" + v->GetVariantName() + "` carries " + expected->ToString() + " but got " +
                          payload->ToString());
      }
      return Set(def, declared->second);
    }
    if (const auto t = instr->AsTestVariant()) {
      const auto subject = TypeOf(t->GetValue());
      if (!(subject->IsAny() || subject->IsNever() || t->GetVariantName() == "Some") &&
          !subject->FindVariant(t->GetVariantName()))
        Report(instr, "`" + t->GetVariantName() + "` can never match a " + subject->ToString());
      return Set(def, CompileType::Bool());
    }
    if (const auto p = instr->AsLoadVariantPayload()) {
      const auto subject = TypeOf(p->GetValue());
      if (subject->IsNever() || subject->IsAny())
        return Set(def, subject);
      if (p->GetVariantName() == "Some")
        return Set(def, subject->WithoutNone());
      const auto variant = subject->FindVariant(p->GetVariantName());
      if (!variant || !variant->GetPayload())
        return Set(def, CompileType::None());
      return Set(def, variant->GetPayload());
    }
    if (const auto c = instr->AsCall())
      return Set(def, CallResult(c));
    // a lambda is analyzed where it is used, which tells it its parameter types
    if (instr->IsNewClosure())
      return false;
    if (const auto phi = instr->AsPhi()) {
      auto type = CompileType::Never();
      for (const auto& in : phi->GetIncoming()) {
        if (!ctx.dead.contains(in.predecessor))
          type = CompileType::Join(type, TypeOf(in.value));
      }
      // the right operand of `&&`/`||` is only reached through a phi
      const auto label = phi->GetJoin()->HasLabel() ? std::string(phi->GetJoin()->GetLabel()->GetData()) : "";
      if (label.starts_with("sc.join") && !type->IsBool() && !type->IsAny() && !type->IsNever())
        Report(instr, "`&&` and `||` need Bool operands, not " + type->ToString());
      return Set(def, type);
    }
    return false;
  }

 public:
  Analyzer(Module* module, ModuleGraph* graphs, std::vector<std::string>* errors) :
    module_(module),
    graphs_(graphs),
    errors_(errors) {}

  auto Run() -> bool {
    for (const auto decl : module_->types)
      decls_[decl->GetName()] = decl;
    for (const auto binding : module_->bindings) {
      if (binding->GetAnnotation())
        annotations_[binding->GetLocal()] = binding->GetAnnotation();
    }

    // resolving the declarations up front reports their errors once, and
    // collects the variant constructors
    for (const auto decl : module_->types) {
      if (!decl->GetParams().empty())
        continue;
      resolving_.insert(decl->GetName());
      const auto type = Resolve(decl->GetDefinition(), {}, "type " + decl->GetName());
      resolving_.erase(decl->GetName());
      CollectVariants(type);
    }

    if (const auto init = graphs_->GetInit()) {
      Analyze(init, {}, true);
      init->SetSignature({}, CompileType::None());
    }
    for (const auto function : module_->functions) {
      if (function->HasBody() || function->IsExtern())
        EnsureAnalyzed(function);
    }
    for (const auto& [local, type] : globals_)
      graphs_->SetGlobalType(local, type);
    return !failed_;
  }
};
}  // namespace

auto TypeAnalysis::Run(Module* module, ModuleGraph* graphs, std::vector<std::string>* errors) -> bool {
  Analyzer analyzer(module, graphs, errors);
  return analyzer.Run();
}
}  // namespace kura
