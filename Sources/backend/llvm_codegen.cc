#include "backend/llvm_codegen.h"

#include <llvm/Config/llvm-config.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Linker/Linker.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

#include "backend/c_compile.h"
#include "backend/compile_type.h"
#include "backend/ir.h"
#include "backend/runtime_abi.h"
#include "c_type.h"
#include "function.h"
#include "local_scope.h"
#include "module.h"
#include "object.h"
#include "property.h"

namespace kura {
auto EntryPointName(const std::string& function) -> std::string {
  std::string name = "kura_fn_";
  for (const char c : function)
    name += (c == '-' ? '_' : c);
  return name;
}

namespace {
// How a value of a type is held in compiled code.
enum class Rep { kInt, kFloat, kBool, kNone, kPtr, kTagged };

auto RepOf(CompileType* type) -> Rep {
  switch (type->GetKind()) {
    case CompileType::kInt:
    case CompileType::kHandle:
      return Rep::kInt;
    case CompileType::kFloat:
    case CompileType::kPx:
    case CompileType::kPct:
    case CompileType::kFr:
      return Rep::kFloat;
    case CompileType::kBool:
      return Rep::kBool;
    case CompileType::kNone:
    case CompileType::kNever:
      return Rep::kNone;
    case CompileType::kString:
    case CompileType::kColor:
    case CompileType::kNode:
    case CompileType::kSeq:
    case CompileType::kRecord:
    case CompileType::kFunction:
      return Rep::kPtr;
    default:
      return Rep::kTagged;
  }
}

// The tag of a value of a concrete (not tagged) type.
auto TagOf(CompileType* type) -> uint64_t {
  switch (type->GetKind()) {
    case CompileType::kInt:
      return rt::kTagInt;
    case CompileType::kFloat:
      return rt::kTagFloat;
    case CompileType::kBool:
      return rt::kTagBool;
    case CompileType::kString:
      return rt::kTagString;
    case CompileType::kColor:
      return rt::kTagColor;
    case CompileType::kPx:
      return rt::kTagPx;
    case CompileType::kPct:
      return rt::kTagPct;
    case CompileType::kFr:
      return rt::kTagFr;
    case CompileType::kNode:
      return rt::kTagNode;
    case CompileType::kSeq:
      return rt::kTagSeq;
    case CompileType::kRecord:
      return rt::kTagRecord;
    case CompileType::kFunction:
      return rt::kTagFunction;
    case CompileType::kHandle:
      return rt::kTagHandle;
    default:
      return rt::kTagNone;
  }
}

class Generator {
 private:
  struct FnState {
    FlowGraph* graph;
    llvm::Function* native;
    std::unordered_map<Definition*, llvm::Value*> values{};
    std::unordered_map<EntryInstr*, llvm::BasicBlock*> blocks{};
    // where each IR block ends: lowering an instruction can add blocks, and
    // that's the block a phi's incoming value comes from
    std::unordered_map<EntryInstr*, llvm::BasicBlock*> ends{};
    std::vector<std::pair<PhiInstr*, llvm::PHINode*>> phis{};
  };

  llvm::LLVMContext& ctx_;
  std::unique_ptr<llvm::Module> module_;
  llvm::IRBuilder<> b_;
  ModuleGraph* graphs_;
  std::string error_{};

  llvm::StructType* value_ty_;
  llvm::PointerType* ptr_ty_;
  llvm::IntegerType* i64_;
  llvm::IntegerType* i8_;
  llvm::IntegerType* i1_;
  llvm::Type* f64_;
  llvm::Type* void_ty_;

  std::unordered_map<FlowGraph*, llvm::Function*> natives_{};
  std::unordered_map<FlowGraph*, llvm::Function*> adapters_{};
  std::unordered_map<FlowGraph*, llvm::Function*> thunks_{};
  std::unordered_map<LocalVariable*, llvm::GlobalVariable*> globals_{};
  std::vector<std::pair<std::string, llvm::GlobalVariable*>> strings_{};
  std::unordered_map<std::string, llvm::GlobalVariable*> string_index_{};
  std::unordered_map<std::string, llvm::Constant*> names_{};
  std::set<std::string> variants_{};
  FnState* fn_ = nullptr;

  void Fail(const std::string& message) {
    if (error_.empty())
      error_ = message;
  }

  // ── types ────────────────────────────────────────────────────────────────
  auto Lower(CompileType* type) -> llvm::Type* {
    switch (RepOf(type)) {
      case Rep::kInt:
        return i64_;
      case Rep::kFloat:
        return f64_;
      case Rep::kBool:
        return i1_;
      case Rep::kNone:
        return i8_;
      case Rep::kPtr:
        return ptr_ty_;
      case Rep::kTagged:
        return value_ty_;
    }
    return i8_;
  }

  // ── tagged values ────────────────────────────────────────────────────────
  auto Pack(llvm::Value* tag, llvm::Value* bits) -> llvm::Value* {
    llvm::Value* value = llvm::PoisonValue::get(value_ty_);
    value = b_.CreateInsertValue(value, tag, 0);
    return b_.CreateInsertValue(value, bits, 1);
  }

  auto TagOfValue(llvm::Value* value) -> llvm::Value* {
    return b_.CreateExtractValue(value, 0);
  }

  auto BitsOfValue(llvm::Value* value) -> llvm::Value* {
    return b_.CreateExtractValue(value, 1);
  }

  auto ToBits(llvm::Value* value, CompileType* type) -> llvm::Value* {
    switch (RepOf(type)) {
      case Rep::kInt:
        return value;
      case Rep::kFloat:
        return b_.CreateBitCast(value, i64_);
      case Rep::kBool:
        return b_.CreateZExt(value, i64_);
      case Rep::kPtr:
        return b_.CreatePtrToInt(value, i64_);
      default:
        return b_.getInt64(0);
    }
  }

  auto FromBits(llvm::Value* bits, CompileType* type) -> llvm::Value* {
    switch (RepOf(type)) {
      case Rep::kInt:
        return bits;
      case Rep::kFloat:
        return b_.CreateBitCast(bits, f64_);
      case Rep::kBool:
        return b_.CreateICmpNE(bits, b_.getInt64(0));
      case Rep::kPtr:
        return b_.CreateIntToPtr(bits, ptr_ty_);
      default:
        return b_.getInt8(0);
    }
  }

  // Converts `value`, which holds a `from`, to hold a `to`. It never branches,
  // so it can be emitted just before the end of a block.
  auto Convert(llvm::Value* value, CompileType* from, CompileType* to) -> llvm::Value* {
    if (from == to)
      return value;
    const auto rf = RepOf(from);
    const auto rt = RepOf(to);
    if (rt == Rep::kTagged) {
      if (rf == Rep::kTagged)
        return value;
      return Pack(b_.getInt64(TagOf(from)), ToBits(value, from));
    }
    if (rf == Rep::kTagged) {
      // checked: the value is only known to be a `to` at runtime
      CallRt("kura_rt_expect_tag", void_ty_, {TagOfValue(value), b_.getInt64(TagOf(to))});
      return FromBits(BitsOfValue(value), to);
    }
    if (rf == rt)
      return value;  // e.g. Seq<Int> to Seq<Number>: the elements are tagged either way
    Fail("internal error: can't convert " + from->ToString() + " to " + to->ToString());
    return llvm::PoisonValue::get(Lower(to));
  }

  // ── runtime calls ────────────────────────────────────────────────────────
  // A tagged argument is passed as its two words.
  auto CallRt(const char* name, llvm::Type* ret, const std::vector<llvm::Value*>& args) -> llvm::Value* {
    std::vector<llvm::Value*> flat{};
    std::vector<llvm::Type*> types{};
    for (const auto arg : args) {
      if (arg->getType() == value_ty_) {
        flat.push_back(TagOfValue(arg));
        flat.push_back(BitsOfValue(arg));
        types.push_back(i64_);
        types.push_back(i64_);
      } else {
        flat.push_back(arg);
        types.push_back(arg->getType());
      }
    }
    const auto callee = module_->getOrInsertFunction(name, llvm::FunctionType::get(ret, types, false));
    return b_.CreateCall(callee, flat);
  }

  auto NameConstant(const std::string& name) -> llvm::Constant* {
    const auto it = names_.find(name);
    if (it != names_.end())
      return it->second;
    return names_[name] = b_.CreateGlobalString(name, "kura.name");
  }

  auto StringLiteral(const std::string& value) -> llvm::Value* {
    auto it = string_index_.find(value);
    if (it == string_index_.end()) {
      const auto global = new llvm::GlobalVariable(*module_, ptr_ty_, false, llvm::GlobalValue::InternalLinkage,
                                                   llvm::ConstantPointerNull::get(ptr_ty_),
                                                   "kura.str." + std::to_string(strings_.size()));
      strings_.push_back({value, global});
      it = string_index_.emplace(value, global).first;
    }
    return b_.CreateLoad(ptr_ty_, it->second);
  }

  // A pointer to the object in `value`, which holds a record, seq or node
  // statically (a pointer) or maybe (tagged).
  auto ObjectPtr(llvm::Value* value, CompileType* type) -> llvm::Value* {
    if (RepOf(type) == Rep::kTagged)
      return b_.CreateIntToPtr(BitsOfValue(value), ptr_ty_);
    return value;
  }

  auto GlobalFor(LocalVariable* local) -> llvm::GlobalVariable* {
    const auto it = globals_.find(local);
    if (it != globals_.end())
      return it->second;
    auto type = graphs_->GetGlobalType(local);
    if (!type)
      type = CompileType::Any();
    const auto lowered = Lower(type);
    const auto global = new llvm::GlobalVariable(*module_, lowered, false, llvm::GlobalValue::InternalLinkage,
                                                 llvm::Constant::getNullValue(lowered),
                                                 "kura.g." + std::string(local->GetName()->GetData()));
    return globals_[local] = global;
  }

  // ── values of the function being generated ───────────────────────────────
  auto Type(Value* value) -> CompileType* {
    const auto def = value->GetDefinition();
    if (!def->HasCompileType()) {
      Fail("internal error: %" + std::to_string(def->GetId()) + " has no type");
      return CompileType::Any();
    }
    return def->GetCompileType();
  }

  auto Val(Value* value) -> llvm::Value* {
    const auto it = fn_->values.find(value->GetDefinition());
    if (it == fn_->values.end()) {
      Fail("internal error: %" + std::to_string(value->GetDefinition()->GetId()) + " is used before it is defined");
      return llvm::PoisonValue::get(i8_);
    }
    return it->second;
  }

  auto As(Value* value, CompileType* to) -> llvm::Value* {
    return Convert(Val(value), Type(value), to);
  }

  // `value` holds a `natural`; the definition's declared type may be wider.
  void Define(Definition* def, llvm::Value* value, CompileType* natural) {
    if (!def->HasCompileType()) {
      Fail("internal error: %" + std::to_string(def->GetId()) + " has no type");
      return;
    }
    fn_->values[def] = Convert(value, natural, def->GetCompileType());
  }

  // ── operators ────────────────────────────────────────────────────────────
  static auto IsFloatLike(CompileType* type) -> bool {
    return RepOf(type) == Rep::kFloat;
  }

  auto Dynamic(BinaryOp op, llvm::Value* a, CompileType* ta, llvm::Value* b, CompileType* tb) -> llvm::Value* {
    return CallRt("kura_rt_binop", value_ty_,
                  {b_.getInt64(static_cast<uint64_t>(op)), Convert(a, ta, CompileType::Any()),
                   Convert(b, tb, CompileType::Any())});
  }

  void EmitBinary(BinaryOpInstr* instr) {
    const auto left = instr->GetLeft();
    const auto right = instr->GetRight();
    const auto a = Val(left);
    const auto b = Val(right);
    const auto ta = Type(left);
    const auto tb = Type(right);
    const auto op = instr->GetOp();
    const auto result = instr->GetCompileType();
    const auto bool_ty = CompileType::Bool();

    const auto is_compare = op == BinaryOp::kEq || op == BinaryOp::kNeq || op == BinaryOp::kLessThan ||
                            op == BinaryOp::kLessThanEqual || op == BinaryOp::kGreaterThan ||
                            op == BinaryOp::kGreaterThanEqual;

    // strings
    if (ta->IsString() && tb->IsString()) {
      if (op == BinaryOp::kAdd)
        return Define(instr, CallRt("kura_rt_string_concat", ptr_ty_, {a, b}), CompileType::String());
      if (is_compare) {
        const auto cmp = CallRt("kura_rt_string_cmp", i64_, {a, b});
        return Define(instr, b_.CreateICmp(IntPredicate(op), cmp, b_.getInt64(0)), bool_ty);
      }
    }
    if (op == BinaryOp::kAdd && ta->IsSeq() && tb->IsSeq())
      return Define(instr, CallRt("kura_rt_seq_concat", ptr_ty_, {a, b}), result);
    if (op == BinaryOp::kAdd && ta->IsRecord() && tb->IsRecord())
      return Define(instr, CallRt("kura_rt_record_merge", ptr_ty_, {a, b}), result);

    // Int op Int
    if (ta->IsInt() && tb->IsInt()) {
      llvm::Value* v = nullptr;
      switch (op) {
        case BinaryOp::kAdd:
          v = b_.CreateAdd(a, b);
          break;
        case BinaryOp::kSubtract:
          v = b_.CreateSub(a, b);
          break;
        case BinaryOp::kMultiply:
          v = b_.CreateMul(a, b);
          break;
        case BinaryOp::kDivide:
          v = CallRt("kura_rt_sdiv", i64_, {a, b});
          break;
        case BinaryOp::kModulus:
          v = CallRt("kura_rt_srem", i64_, {a, b});
          break;
        default:
          if (is_compare)
            return Define(instr, b_.CreateICmp(IntPredicate(op), a, b), bool_ty);
      }
      if (v)
        return Define(instr, v, CompileType::Int());
    }

    // Float and unit arithmetic: both are doubles
    if (IsFloatLike(ta) && IsFloatLike(tb)) {
      if (is_compare && ta == tb)
        return Define(instr, b_.CreateFCmp(FloatPredicate(op), a, b), bool_ty);
      llvm::Value* v = nullptr;
      switch (op) {
        case BinaryOp::kAdd:
          v = b_.CreateFAdd(a, b);
          break;
        case BinaryOp::kSubtract:
          v = b_.CreateFSub(a, b);
          break;
        case BinaryOp::kMultiply:
          v = b_.CreateFMul(a, b);
          break;
        case BinaryOp::kDivide:
          v = b_.CreateFDiv(a, b);
          break;
        case BinaryOp::kModulus:
          v = b_.CreateFRem(a, b);
          break;
        default:
          break;
      }
      if (v) {
        // a unit scaled by a Float is that unit, and a ratio of two is a Float
        if (ta == tb)
          return Define(instr, v, op == BinaryOp::kDivide && ta->IsUnit() ? CompileType::Float() : ta);
        return Define(instr, v, ta->IsUnit() ? ta : tb);
      }
    }

    // a unit scaled by an Int
    if (ta->IsUnit() && tb->IsInt() && (op == BinaryOp::kMultiply || op == BinaryOp::kDivide)) {
      const auto scale = b_.CreateSIToFP(b, f64_);
      return Define(instr, op == BinaryOp::kMultiply ? b_.CreateFMul(a, scale) : b_.CreateFDiv(a, scale), ta);
    }
    if (ta->IsInt() && tb->IsUnit() && op == BinaryOp::kMultiply)
      return Define(instr, b_.CreateFMul(b_.CreateSIToFP(a, f64_), b), tb);

    if (ta->IsBool() && tb->IsBool()) {
      switch (op) {
        case BinaryOp::kEq:
          return Define(instr, b_.CreateICmpEQ(a, b), bool_ty);
        case BinaryOp::kNeq:
          return Define(instr, b_.CreateICmpNE(a, b), bool_ty);
        case BinaryOp::kBinaryAnd:
          return Define(instr, b_.CreateAnd(a, b), bool_ty);
        case BinaryOp::kBinaryOr:
          return Define(instr, b_.CreateOr(a, b), bool_ty);
        default:
          break;
      }
    }

    // Number, Any, unions and everything else: decided from the tags at runtime
    Define(instr, Dynamic(op, a, ta, b, tb), CompileType::Any());
  }

  static auto IntPredicate(BinaryOp op) -> llvm::CmpInst::Predicate {
    switch (op) {
      case BinaryOp::kEq:
        return llvm::CmpInst::ICMP_EQ;
      case BinaryOp::kNeq:
        return llvm::CmpInst::ICMP_NE;
      case BinaryOp::kLessThan:
        return llvm::CmpInst::ICMP_SLT;
      case BinaryOp::kLessThanEqual:
        return llvm::CmpInst::ICMP_SLE;
      case BinaryOp::kGreaterThan:
        return llvm::CmpInst::ICMP_SGT;
      default:
        return llvm::CmpInst::ICMP_SGE;
    }
  }

  static auto FloatPredicate(BinaryOp op) -> llvm::CmpInst::Predicate {
    switch (op) {
      case BinaryOp::kEq:
        return llvm::CmpInst::FCMP_OEQ;
      case BinaryOp::kNeq:
        return llvm::CmpInst::FCMP_UNE;
      case BinaryOp::kLessThan:
        return llvm::CmpInst::FCMP_OLT;
      case BinaryOp::kLessThanEqual:
        return llvm::CmpInst::FCMP_OLE;
      case BinaryOp::kGreaterThan:
        return llvm::CmpInst::FCMP_OGT;
      default:
        return llvm::CmpInst::FCMP_OGE;
    }
  }

  void EmitUnary(UnaryOpInstr* instr) {
    const auto value = Val(instr->GetValue());
    const auto type = Type(instr->GetValue());
    const auto op = instr->GetOp();
    if (op == UnaryOp::kBang && type->IsBool())
      return Define(instr, b_.CreateNot(value), type);
    if (op == UnaryOp::kPlus && RepOf(type) != Rep::kTagged)
      return Define(instr, value, type);
    if (op == UnaryOp::kMinus) {
      if (type->IsInt())
        return Define(instr, b_.CreateNeg(value), type);
      if (IsFloatLike(type))
        return Define(instr, b_.CreateFNeg(value), type);
    }
    Define(instr,
           CallRt("kura_rt_unop", value_ty_,
                  {b_.getInt64(static_cast<uint64_t>(op)), Convert(value, type, CompileType::Any())}),
           CompileType::Any());
  }

  // ── calls ────────────────────────────────────────────────────────────────
  // A direct call to an extern that needs the call site: a variadic one
  // (`printf("%d", n)`), or one that takes a callback, which is the name of a
  // function.
  void EmitExternCall(CallInstr* call, FlowGraph* graph) {
    const auto& c = *graph->GetFunction()->GetCSignature();
    const auto& args = call->GetArgs();
    const auto& kura_params = graph->GetParameterTypes();
    std::vector<llvm::Value*> values{};
    size_t next = 0;
    for (const auto& param : c.params) {
      if (param.kind == CType::kOut)
        continue;
      values.push_back(param.kind == CType::kCallback ? nullptr : As(args[next], kura_params[next]));
      next++;
    }
    for (; next < args.size(); next++)
      values.push_back(Val(args[next]));
    Define(call, CallC(graph, values, call), graph->GetReturnType());
  }

  void EmitCall(CallInstr* call) {
    const auto& args = call->GetArgs();
    const auto target = call->GetTarget();

    // a module function called by name is a direct call in its own signature
    if (const auto load = target->GetDefinition()->AsLoadFunction()) {
      const auto graph = graphs_->GetGraphFor(load->GetFunction());
      const auto native = graph ? natives_[graph] : nullptr;
      if (native && graph->IsExtern() &&
          (load->GetFunction()->GetCSignature()->variadic || load->GetFunction()->GetCSignature()->HasCallbacks()))
        return EmitExternCall(call, graph);
      if (native) {
        const auto& params = graph->GetParameterTypes();
        std::vector<llvm::Value*> values{};
        for (size_t idx = 0; idx < args.size(); idx++)
          values.push_back(As(args[idx], params[idx]));
        return Define(call, b_.CreateCall(native, values), graph->GetReturnType());
      }
    }

    // anything else is a closure: its thunk takes tagged arguments
    const auto closure_ty = Type(target);
    const auto closure =
        RepOf(closure_ty) == Rep::kPtr ? Val(target) : As(target, CompileType::Function({}, CompileType::Any()));
    const auto fn = CallRt("kura_rt_closure_fn", ptr_ty_, {closure, b_.getInt64(args.size())});
    std::vector<llvm::Type*> types{ptr_ty_};
    std::vector<llvm::Value*> values{closure};
    for (const auto arg : args) {
      const auto boxed = As(arg, CompileType::Any());
      types.push_back(i64_);
      types.push_back(i64_);
      values.push_back(TagOfValue(boxed));
      values.push_back(BitsOfValue(boxed));
    }
    const auto result = b_.CreateCall(llvm::FunctionType::get(value_ty_, types, false), fn, values);
    Define(call, result, CompileType::Any());
  }

  // ── instructions ─────────────────────────────────────────────────────────
  void EmitConstant(ConstantInstr* instr) {
    const auto value = instr->GetValue();
    switch (value->GetType()) {
      case kIntType:
        return Define(instr, b_.getInt64(static_cast<Int*>(value)->GetValue()), CompileType::Int());
      case kFloatType:
        return Define(instr, llvm::ConstantFP::get(f64_, static_cast<Float*>(value)->GetValue()), CompileType::Float());
      case kPxType:
        return Define(instr, llvm::ConstantFP::get(f64_, static_cast<Px*>(value)->GetValue()), CompileType::Px());
      case kPctType:
        return Define(instr, llvm::ConstantFP::get(f64_, static_cast<Pct*>(value)->GetValue()), CompileType::Pct());
      case kFrType:
        return Define(instr, llvm::ConstantFP::get(f64_, static_cast<Fr*>(value)->GetValue()), CompileType::Fr());
      case kBoolType:
        return Define(instr, b_.getInt1(static_cast<Bool*>(value)->GetValue()), CompileType::Bool());
      case kNoneType:
        return Define(instr, b_.getInt8(0), CompileType::None());
      case kStringType:
        return Define(instr, StringLiteral(static_cast<String*>(value)->GetValue()), CompileType::String());
      case kColorType:
        return Define(instr, StringLiteral(static_cast<Color*>(value)->GetValue()), CompileType::Color());
      default:
        Fail("internal error: can't generate a constant of " + value->ToString());
    }
  }

  void EmitInstruction(Instruction* instr) {
    if (const auto c = instr->AsConstant())
      return EmitConstant(c);
    if (const auto p = instr->AsParameter())
      return Define(p, fn_->native->getArg(p->GetIndex()), p->GetCompileType());
    if (const auto b = instr->AsBinaryOp())
      return EmitBinary(b);
    if (const auto u = instr->AsUnaryOp())
      return EmitUnary(u);
    if (const auto c = instr->AsCall())
      return EmitCall(c);

    if (const auto phi = instr->AsPhi()) {
      const auto node = b_.CreatePHI(Lower(phi->GetCompileType()), phi->GetNumberOfIncoming());
      fn_->values[phi] = node;
      fn_->phis.push_back({phi, node});
      return;
    }

    if (const auto l = instr->AsLoadFunction()) {
      const auto graph = graphs_->GetGraphFor(l->GetFunction());
      const auto thunk = graph ? thunks_[graph] : nullptr;
      if (!thunk)
        return Fail("internal error: no code for function `" + std::string(l->GetFunction()->GetName()->GetData()) +
                    "`");
      const auto closure = CallRt("kura_rt_closure_new", ptr_ty_,
                                  {thunk, b_.getInt64(graph->GetParameterTypes().size()), b_.getInt64(0)});
      return Define(l, closure, l->GetCompileType());
    }

    if (const auto n = instr->AsNewClosure()) {
      const auto graph = n->GetTarget();
      const auto thunk = thunks_[graph];
      if (!thunk)
        return Fail("internal error: no code for " + graph->GetName());
      const auto arity = graph->GetNumberOfParameters() - graph->GetCaptures().size();
      const auto closure =
          CallRt("kura_rt_closure_new", ptr_ty_, {thunk, b_.getInt64(arity), b_.getInt64(n->GetCaptures().size())});
      uint64_t index = 0;
      for (const auto capture : n->GetCaptures())
        CallRt("kura_rt_closure_capture", void_ty_, {closure, b_.getInt64(index++), As(capture, CompileType::Any())});
      return Define(n, closure, n->GetCompileType());
    }

    if (const auto l = instr->AsLoadLocal()) {
      const auto global = GlobalFor(l->GetLocal());
      return Define(l, b_.CreateLoad(global->getValueType(), global),
                    graphs_->GetGlobalType(l->GetLocal()) ? graphs_->GetGlobalType(l->GetLocal()) : CompileType::Any());
    }
    if (const auto s = instr->AsStoreLocal()) {
      const auto global = GlobalFor(s->GetLocal());
      auto type = graphs_->GetGlobalType(s->GetLocal());
      if (!type)
        type = CompileType::Any();
      b_.CreateStore(As(s->GetValue(), type), global);
      return;
    }

    if (const auto l = instr->AsNewList()) {
      const auto seq = CallRt("kura_rt_seq_new", ptr_ty_, {b_.getInt64(l->GetInputs().size())});
      uint64_t index = 0;
      for (const auto input : l->GetInputs())
        CallRt("kura_rt_store_index", void_ty_, {seq, b_.getInt64(index++), As(input, CompileType::Any())});
      return Define(l, seq, l->GetCompileType());
    }
    if (const auto s = instr->AsStoreIndex()) {
      const auto object = ObjectPtr(Val(s->GetInstance()), Type(s->GetInstance()));
      CallRt("kura_rt_store_index", void_ty_,
             {object, As(s->GetIndex(), CompileType::Int()), As(s->GetValue(), CompileType::Any())});
      return;
    }
    if (const auto l = instr->AsLoadIndex()) {
      const auto object = ObjectPtr(Val(l->GetInstance()), Type(l->GetInstance()));
      return Define(l, CallRt("kura_rt_seq_get", value_ty_, {object, As(l->GetIndex(), CompileType::Int())}),
                    CompileType::Any());
    }
    if (const auto l = instr->AsLoadLength()) {
      const auto object = ObjectPtr(Val(l->GetInstance()), Type(l->GetInstance()));
      return Define(l, CallRt("kura_rt_len", i64_, {object}), CompileType::Int());
    }

    if (const auto r = instr->AsNewRecord())
      return Define(r, CallRt("kura_rt_record_new", ptr_ty_, {}), r->GetCompileType());
    if (const auto s = instr->AsStoreProperty()) {
      const auto object = ObjectPtr(Val(s->GetInstance()), Type(s->GetInstance()));
      const auto name = std::string(s->GetProperty()->GetPropertyName()->GetData());
      CallRt("kura_rt_record_set", void_ty_,
             {object, b_.getInt64(rt::HashName(name)), NameConstant(name), As(s->GetValue(), CompileType::Any())});
      return;
    }
    if (const auto s = instr->AsSpread()) {
      CallRt("kura_rt_record_spread", void_ty_,
             {ObjectPtr(Val(s->GetInstance()), Type(s->GetInstance())),
              ObjectPtr(Val(s->GetValue()), Type(s->GetValue()))});
      return;
    }
    if (const auto n = instr->AsNewNode()) {
      std::string kind = "node";
      switch (n->GetKind()) {
#define DEFINE_CASE(Name)           \
  case NewNodeInstr::Kind::k##Name: \
    kind = ToLower(#Name);          \
    break;
        FOR_EACH_ELEMENT_NODE(DEFINE_CASE)
#undef DEFINE_CASE
        default:
          break;
      }
      return Define(n, CallRt("kura_rt_node_new", ptr_ty_, {NameConstant(kind)}), n->GetCompileType());
    }

    if (const auto l = instr->AsLoadProperty())
      return EmitLoadProperty(l, l->GetInstance(), l->GetProperty(), false);
    if (const auto l = instr->AsLoadPropertySafely())
      return EmitLoadProperty(l, l->GetInstance(), l->GetProperty(), true);

    if (const auto v = instr->AsNewVariant()) {
      if (v->GetVariantName() == "Some")
        return Define(v, Val(v->GetPayload()), Type(v->GetPayload()));
      variants_.insert(v->GetVariantName());
      llvm::Value* box = llvm::ConstantPointerNull::get(ptr_ty_);
      if (v->HasPayload())
        box = CallRt("kura_rt_box", ptr_ty_, {As(v->GetPayload(), CompileType::Any())});
      return Define(v, Pack(b_.getInt64(rt::VariantTag(v->GetVariantName())), b_.CreatePtrToInt(box, i64_)),
                    CompileType::Any());
    }
    if (const auto t = instr->AsTestVariant()) {
      const auto type = Type(t->GetValue());
      const auto value = Val(t->GetValue());
      if (RepOf(type) != Rep::kTagged) {
        // a value of a concrete type is `Some` unless it is None
        return Define(t, b_.getInt1(t->GetVariantName() == "Some" && !type->IsNone()), CompileType::Bool());
      }
      if (t->GetVariantName() == "Some")
        return Define(t, b_.CreateICmpNE(TagOfValue(value), b_.getInt64(rt::kTagNone)), CompileType::Bool());
      variants_.insert(t->GetVariantName());
      return Define(t, b_.CreateICmpEQ(TagOfValue(value), b_.getInt64(rt::VariantTag(t->GetVariantName()))),
                    CompileType::Bool());
    }
    if (const auto p = instr->AsLoadVariantPayload()) {
      const auto type = Type(p->GetValue());
      const auto value = Val(p->GetValue());
      if (p->GetVariantName() == "Some")
        return Define(p, value, type);  // `Some(x)` is x
      if (RepOf(type) != Rep::kTagged)
        return Define(p, b_.getInt8(0), CompileType::None());
      const auto box = b_.CreateIntToPtr(BitsOfValue(value), ptr_ty_);
      return Define(p, CallRt("kura_rt_unbox", value_ty_, {box}), CompileType::Any());
    }

    if (instr->IsGraphEntry() || instr->IsTargetEntry() || instr->IsJoinEntry() || instr->IsLabel())
      return;
    Fail("internal error: can't generate " + instr->ToString());
  }

  static auto ToLower(std::string text) -> std::string {
    for (auto& c : text)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
  }

  void EmitLoadProperty(Definition* def, Value* instance, Property* property, const bool safe) {
    const auto type = Type(instance);
    const auto value = Val(instance);
    const auto hash = b_.getInt64(rt::HashName(std::string(property->GetPropertyName()->GetData())));
    if (RepOf(type) == Rep::kTagged) {
      return Define(def, CallRt(safe ? "kura_rt_get_safe" : "kura_rt_get_dyn", value_ty_, {value, hash}),
                    CompileType::Any());
    }
    Define(def, CallRt("kura_rt_record_get", value_ty_, {value, hash}), CompileType::Any());
  }

  // ── blocks and functions ─────────────────────────────────────────────────
  void EmitBlock(EntryInstr* block) {
    const auto bb = fn_->blocks[block];
    b_.SetInsertPoint(bb);
    if (fn_->graph->IsDead(block)) {
      b_.CreateUnreachable();
      fn_->ends[block] = bb;
      return;
    }

    for (Instruction* instr = block->GetNext(); instr != nullptr; instr = instr->GetNext()) {
      if (const auto branch = instr->AsBranch()) {
        const auto condition = As(branch->GetCondition(), CompileType::Bool());
        fn_->ends[block] = b_.GetInsertBlock();
        const auto otherwise = branch->HasElse() ? static_cast<EntryInstr*>(branch->GetElse())
                                                 : static_cast<EntryInstr*>(branch->GetJoin());
        b_.CreateCondBr(condition, fn_->blocks[branch->GetThen()], fn_->blocks[otherwise]);
        return;
      }
      if (const auto go = instr->AsGoto()) {
        fn_->ends[block] = b_.GetInsertBlock();
        b_.CreateBr(fn_->blocks[go->GetTarget()]);
        return;
      }
      if (const auto ret = instr->AsReturn()) {
        const auto value = As(ret->GetValue(), fn_->graph->GetReturnType());
        fn_->ends[block] = b_.GetInsertBlock();
        b_.CreateRet(value);
        return;
      }
      EmitInstruction(instr);
    }
    Fail("internal error: B" + std::to_string(block->GetBlockId()) + " of " + fn_->graph->GetName() +
         " has no terminator");
  }

  // ── C interop ────────────────────────────────────────────────────────────
  auto CAbi(const CType& c) -> llvm::Type* {
    switch (c.kind) {
      case CType::kVoid:
        return void_ty_;
      case CType::kBool:
        return i1_;
      case CType::kI8:
      case CType::kU8:
        return i8_;
      case CType::kI16:
      case CType::kU16:
        return llvm::Type::getInt16Ty(ctx_);
      case CType::kI32:
      case CType::kU32:
        return llvm::Type::getInt32Ty(ctx_);
      case CType::kI64:
      case CType::kU64:
        return i64_;
      case CType::kF32:
        return llvm::Type::getFloatTy(ctx_);
      case CType::kF64:
        return f64_;
      case CType::kCString:
      case CType::kHandle:
      case CType::kOut:
      case CType::kCallback:
      case CType::kRecord:
        return ptr_ty_;
    }
    return void_ty_;
  }

  // The C ABI wants a value narrower than an int extended to one.
  static auto Extension(const CType& c) -> llvm::Attribute::AttrKind {
    if (c.kind == CType::kBool)
      return llvm::Attribute::ZExt;
    if (c.IsInteger() && c.Bits() < 32)
      return c.IsSigned() ? llvm::Attribute::SExt : llvm::Attribute::ZExt;
    return llvm::Attribute::None;
  }

  auto Describe(const std::string& text) -> llvm::Value* {
    return b_.CreateGlobalString(text, "kura.ffi");
  }

  // An Int on its way to a narrower C integer: it must be exactly representable.
  auto NarrowToC(llvm::Value* value, const CType& c, const std::string& what) -> llvm::Value* {
    if (c.kind == CType::kI64)
      return value;
    llvm::Value* narrow = value;
    llvm::Value* fits = nullptr;
    if (c.kind == CType::kU64) {
      fits = b_.CreateICmpSGE(value, b_.getInt64(0));
    } else {
      narrow = b_.CreateTrunc(value, CAbi(c));
      const auto back = c.IsSigned() ? b_.CreateSExt(narrow, i64_) : b_.CreateZExt(narrow, i64_);
      fits = b_.CreateICmpEQ(back, value);
    }
    const auto function = b_.GetInsertBlock()->getParent();
    const auto good = llvm::BasicBlock::Create(ctx_, "ffi.ok", function);
    const auto bad = llvm::BasicBlock::Create(ctx_, "ffi.overflow", function);
    b_.CreateCondBr(fits, good, bad);
    b_.SetInsertPoint(bad);
    CallRt("kura_rt_ffi_overflow", void_ty_, {value, Describe(what)});
    b_.CreateUnreachable();
    b_.SetInsertPoint(good);
    return narrow;
  }

  // `value` holds a Kura value of `kura`; the result is what C is passed.
  auto ToC(llvm::Value* value, CompileType* kura, const CType& c, const std::string& what) -> llvm::Value* {
    switch (c.kind) {
      case CType::kBool:
        return value;
      case CType::kF32:
        return b_.CreateFPTrunc(value, CAbi(c));
      case CType::kF64:
        return value;
      case CType::kCString: {
        llvm::Value* object = value;
        if (RepOf(kura) == Rep::kTagged)
          object = b_.CreateIntToPtr(BitsOfValue(value), ptr_ty_);
        return CallRt("kura_rt_string_cstr", ptr_ty_, {object});
      }
      case CType::kHandle: {
        llvm::Value* bits = value;
        if (RepOf(kura) == Rep::kTagged)
          bits = BitsOfValue(value);
        return b_.CreateIntToPtr(bits, ptr_ty_);
      }
      default:
        return NarrowToC(value, c, what + " (" + c.ToString() + ")");
    }
  }

  // What C returned, as a Kura value of `kura`.
  auto FromC(llvm::Value* value, const CType& c, CompileType* kura, const std::string& what) -> llvm::Value* {
    switch (c.kind) {
      case CType::kVoid:
        return b_.getInt8(0);
      case CType::kBool:
      case CType::kF64:
        return value;
      case CType::kF32:
        return b_.CreateFPExt(value, f64_);
      case CType::kCString: {
        if (!c.nullable)
          return CallRt("kura_rt_string_from_cstr", ptr_ty_, {value, Describe(what)});
        const auto object = CallRt("kura_rt_string_from_cstr_opt", ptr_ty_, {value});
        const auto missing = b_.CreateIsNull(object);
        const auto tag = b_.CreateSelect(missing, b_.getInt64(rt::kTagNone), b_.getInt64(rt::kTagString));
        return Pack(tag, b_.CreatePtrToInt(object, i64_));
      }
      case CType::kHandle: {
        const auto bits = b_.CreatePtrToInt(value, i64_);
        if (c.nullable) {
          const auto tag =
              b_.CreateSelect(b_.CreateIsNull(value), b_.getInt64(rt::kTagNone), b_.getInt64(rt::kTagHandle));
          return Pack(tag, bits);
        }
        const auto function = b_.GetInsertBlock()->getParent();
        const auto good = llvm::BasicBlock::Create(ctx_, "ffi.ok", function);
        const auto bad = llvm::BasicBlock::Create(ctx_, "ffi.null", function);
        b_.CreateCondBr(b_.CreateIsNull(value), bad, good);
        b_.SetInsertPoint(bad);
        CallRt("kura_rt_ffi_error", void_ty_,
               {Describe(what + " returned NULL, but its result is not declared `| None`")});
        b_.CreateUnreachable();
        b_.SetInsertPoint(good);
        return bits;
      }
      default:
        break;
    }
    if (c.kind == CType::kU64) {
      const auto function = b_.GetInsertBlock()->getParent();
      const auto good = llvm::BasicBlock::Create(ctx_, "ffi.ok", function);
      const auto bad = llvm::BasicBlock::Create(ctx_, "ffi.large", function);
      b_.CreateCondBr(b_.CreateICmpSLT(value, b_.getInt64(0)), bad, good);
      b_.SetInsertPoint(bad);
      CallRt("kura_rt_ffi_error", void_ty_, {Describe("the result of " + what + " is too large for an Int")});
      b_.CreateUnreachable();
      b_.SetInsertPoint(good);
      return value;
    }
    if (c.Bits() == 64)
      return value;
    return c.IsSigned() ? b_.CreateSExt(value, i64_) : b_.CreateZExt(value, i64_);
  }

  auto FieldPtr(llvm::Value* base, const uint64_t offset) -> llvm::Value* {
    return offset ? b_.CreateConstInBoundsGEP1_64(i8_, base, offset) : base;
  }

  // Copies a record's fields into C memory, laid out as the struct is.
  void StoreCRecord(llvm::Value* record, const CRecord& rec, llvm::Value* dst, const std::string& what) {
    for (const auto& field : rec.fields) {
      const auto tagged = CallRt("kura_rt_record_get", value_ty_, {record, b_.getInt64(rt::HashName(field.name))});
      const auto kura = field.type.ToKuraType();
      const auto value = Convert(tagged, CompileType::Any(), kura);
      const auto where = FieldPtr(dst, field.offset);
      const auto label = what + " field `" + field.name + "`";
      if (field.type.kind == CType::kRecord)
        StoreCRecord(value, *field.type.record, where, label);
      else
        b_.CreateStore(ToC(value, kura, field.type, label), where);
    }
  }

  // Builds a record from the fields of a struct in C memory.
  auto LoadCRecord(const CRecord& rec, llvm::Value* src, const std::string& what) -> llvm::Value* {
    const auto record = CallRt("kura_rt_record_new", ptr_ty_, {});
    for (const auto& field : rec.fields) {
      const auto where = FieldPtr(src, field.offset);
      const auto kura = field.type.ToKuraType();
      const auto label = what + " field `" + field.name + "`";
      llvm::Value* value = nullptr;
      if (field.type.kind == CType::kRecord)
        value = LoadCRecord(*field.type.record, where, label);
      else
        value = FromC(b_.CreateLoad(CAbi(field.type), where), field.type, kura, label);
      CallRt("kura_rt_record_set", void_ty_,
             {record, b_.getInt64(rt::HashName(field.name)), NameConstant(field.name),
              Convert(value, kura, CompileType::Any())});
    }
    return record;
  }

  // A function with a struct in its signature, or no symbol of its own, is
  // reached through a C function that takes every argument by pointer (it is
  // compiled by clang, which does the calling convention).
  void DefineShimNative(FlowGraph* graph) {
    const auto function = graph->GetFunction();
    const auto& c = *function->GetCSignature();
    const auto native = natives_[graph];
    const auto name = std::string(function->GetName()->GetData());
    b_.SetInsertPoint(llvm::BasicBlock::Create(ctx_, "entry", native));

    const auto slot = [&](const CType& type) -> llvm::Value* {
      if (type.kind == CType::kRecord) {
        const auto alloca = b_.CreateAlloca(llvm::ArrayType::get(i8_, type.record->size));
        alloca->setAlignment(llvm::Align(type.record->align));
        return alloca;
      }
      return b_.CreateAlloca(CAbi(type));
    };
    std::vector<llvm::Value*> slots{};
    for (const auto& param : c.params)
      slots.push_back(slot(param));
    const auto result_slot = c.result.kind == CType::kVoid ? nullptr : slot(c.result);

    const auto& kura_params = graph->GetParameterTypes();
    for (size_t idx = 0; idx < c.params.size(); idx++) {
      const auto what = "argument " + std::to_string(idx + 1) + " of `" + name + "`";
      if (c.params[idx].kind == CType::kRecord)
        StoreCRecord(native->getArg(idx), *c.params[idx].record, slots[idx], what);
      else
        b_.CreateStore(ToC(native->getArg(idx), kura_params[idx], c.params[idx], what), slots[idx]);
    }
    std::vector<llvm::Value*> args = slots;
    if (result_slot)
      args.push_back(result_slot);
    const auto shim = module_->getOrInsertFunction(
        c.shim_symbol, llvm::FunctionType::get(void_ty_, std::vector<llvm::Type*>(args.size(), ptr_ty_), false));
    b_.CreateCall(shim, args);

    if (!result_slot) {
      b_.CreateRet(b_.getInt8(0));
      return;
    }
    if (c.result.kind == CType::kRecord) {
      b_.CreateRet(LoadCRecord(*c.result.record, result_slot, "the result of `" + name + "`"));
      return;
    }
    b_.CreateRet(FromC(b_.CreateLoad(CAbi(c.result), result_slot), c.result, graph->GetReturnType(), "`" + name + "`"));
  }

  // The C function a callback parameter takes: Kura's function, behind C's
  // calling convention.
  auto CallbackAdapter(FlowGraph* target, const CSignature& callback, const std::string& what) -> llvm::Function* {
    if (const auto it = adapters_.find(target); it != adapters_.end())
      return it->second;
    std::vector<llvm::Type*> types{};
    for (const auto& param : callback.params)
      types.push_back(CAbi(param));
    const auto adapter =
        llvm::Function::Create(llvm::FunctionType::get(CAbi(callback.result), types, false),
                               llvm::GlobalValue::InternalLinkage, "kura.callback." + target->GetName(), module_.get());
    adapters_[target] = adapter;
    for (size_t idx = 0; idx < callback.params.size(); idx++) {
      if (const auto ext = Extension(callback.params[idx]); ext != llvm::Attribute::None)
        adapter->addParamAttr(idx, ext);
    }
    if (const auto ext = Extension(callback.result); ext != llvm::Attribute::None)
      adapter->addRetAttr(ext);
    llvm::IRBuilderBase::InsertPointGuard guard(b_);
    b_.SetInsertPoint(llvm::BasicBlock::Create(ctx_, "entry", adapter));
    const auto& target_params = target->GetParameterTypes();
    if (target_params.size() != callback.params.size()) {
      Fail(what + ": the callback `" + target->GetName() + "` takes " + std::to_string(target_params.size()) +
           " argument(s) but C calls it with " + std::to_string(callback.params.size()));
      b_.CreateUnreachable();
      return adapter;
    }
    std::vector<llvm::Value*> args{};
    for (size_t idx = 0; idx < callback.params.size(); idx++) {
      const auto kura = callback.params[idx].ToKuraType();
      const auto value = FromC(adapter->getArg(idx), callback.params[idx], kura, what + " callback argument");
      args.push_back(Convert(value, kura, target_params[idx]));
    }
    const auto result = b_.CreateCall(natives_[target], args);
    if (callback.result.kind == CType::kVoid) {
      b_.CreateRetVoid();
    } else {
      const auto declared = callback.result.ToKuraType();
      b_.CreateRet(ToC(Convert(result, target->GetReturnType(), declared), declared, callback.result,
                       "the result of the callback `" + target->GetName() + "`"));
    }
    return adapter;
  }

  // What to hand C for a callback parameter: a function in Kura is the name of
  // a function; a closure has no address C could call.
  auto CallbackPointer(const CType& type, Value* arg, const std::string& what) -> llvm::Value* {
    const auto null = llvm::ConstantPointerNull::get(ptr_ty_);
    const auto load = arg ? arg->GetDefinition()->AsLoadFunction() : nullptr;
    if (!arg) {
      CallRt("kura_rt_ffi_error", void_ty_,
             {Describe(what + " is a callback, which can only be passed when the function is called directly")});
      return null;
    }
    if (!load) {
      Fail(what + ": a callback must be the name of a function, not a closure or a value");
      return null;
    }
    const auto target = graphs_->GetGraphFor(load->GetFunction());
    if (!target || target->IsExtern() || !natives_.contains(target)) {
      Fail(what + ": a callback must be a function written in Kura");
      return null;
    }
    return CallbackAdapter(target, *type.callback, what);
  }

  // Calls the C function of an extern. `args` are the Kura values of the
  // parameters a Kura call passes (`KuraParams`), then the extra arguments of a
  // variadic function, which are passed as what they are: an Int is a `long`, a
  // Float a `double`, a Bool an `int`, a String a `const char *`, a handle a
  // pointer. `site` is the call being compiled, if any. The result is C's, in
  // Kura's terms, or a record of it and what C wrote through `c.out` pointers.
  auto CallC(FlowGraph* graph, const std::vector<llvm::Value*>& args, CallInstr* site) -> llvm::Value* {
    const auto function = graph->GetFunction();
    const auto& c = *function->GetCSignature();
    const auto name = std::string(function->GetName()->GetData());
    std::vector<llvm::Type*> param_types{};
    for (const auto& param : c.params)
      param_types.push_back(CAbi(param));
    const auto callee =
        module_->getOrInsertFunction(c.symbol, llvm::FunctionType::get(CAbi(c.result), param_types, c.variadic));

    const auto& kura_params = graph->GetParameterTypes();
    std::vector<llvm::Value*> values{};
    std::vector<llvm::Value*> slots(c.params.size(), nullptr);
    size_t next = 0;
    for (size_t idx = 0; idx < c.params.size(); idx++) {
      const auto& param = c.params[idx];
      const auto what = "argument " + std::to_string(idx + 1) + " of `" + name + "`";
      if (param.kind == CType::kOut) {
        const auto type = CAbi(*param.pointee);
        slots[idx] = b_.CreateAlloca(type);
        b_.CreateStore(llvm::Constant::getNullValue(type), slots[idx]);
        values.push_back(slots[idx]);
      } else if (param.kind == CType::kCallback) {
        values.push_back(CallbackPointer(param, site ? site->GetArgs()[next] : nullptr, what));
        next++;
      } else {
        values.push_back(ToC(args[next], kura_params[next], param, what));
        next++;
      }
    }
    for (; next < args.size(); next++) {
      const auto type = Type(site->GetArgs()[next]);
      switch (RepOf(type)) {
        case Rep::kInt:
          values.push_back(type->IsHandle() ? b_.CreateIntToPtr(args[next], ptr_ty_) : args[next]);
          break;
        case Rep::kFloat:
          values.push_back(args[next]);
          break;
        case Rep::kBool:
          values.push_back(b_.CreateZExt(args[next], llvm::Type::getInt32Ty(ctx_)));
          break;
        case Rep::kPtr:
          values.push_back(CallRt("kura_rt_string_cstr", ptr_ty_, {args[next]}));
          break;
        default:
          Fail("internal error: can't pass " + type->ToString() + " to `" + name + "`");
          return llvm::UndefValue::get(Lower(graph->GetReturnType()));
      }
    }
    const auto call = b_.CreateCall(callee, values);
    for (size_t idx = 0; idx < c.params.size(); idx++) {
      if (const auto ext = Extension(c.params[idx]); ext != llvm::Attribute::None)
        call->addParamAttr(idx, ext);
    }
    if (const auto ext = Extension(c.result); ext != llvm::Attribute::None)
      call->addRetAttr(ext);
    const auto kura_result = c.result.ToKuraType();
    const auto result = FromC(call, c.result, kura_result, "`" + name + "`");
    if (!c.HasOuts())
      return result;

    const auto record = CallRt("kura_rt_record_new", ptr_ty_, {});
    const auto set = [&](const std::string& field, llvm::Value* value, CompileType* type) {
      CallRt("kura_rt_record_set", void_ty_,
             {record, b_.getInt64(rt::HashName(field)), NameConstant(field), Convert(value, type, CompileType::Any())});
    };
    if (c.result.kind != CType::kVoid)
      set("result", result, kura_result);
    for (size_t idx = 0; idx < c.params.size(); idx++) {
      if (c.params[idx].kind != CType::kOut)
        continue;
      const auto& pointee = *c.params[idx].pointee;
      const auto value = FromC(b_.CreateLoad(CAbi(pointee), slots[idx]), pointee, pointee.ToKuraType(),
                               "`" + c.OutName(idx) + "` of `" + name + "`");
      set(c.OutName(idx), value, pointee.ToKuraType());
    }
    return record;
  }

  // The body of an `extern fn` import: convert the arguments, call C, convert
  // the result.
  void DefineExternNative(FlowGraph* graph) {
    if (!graph->GetFunction()->GetCSignature()->shim_symbol.empty())
      return DefineShimNative(graph);
    const auto native = natives_[graph];
    b_.SetInsertPoint(llvm::BasicBlock::Create(ctx_, "entry", native));
    std::vector<llvm::Value*> args{};
    for (auto& arg : native->args())
      args.push_back(&arg);
    b_.CreateRet(CallC(graph, args, nullptr));
  }

  // C's view of an argument it passes in: the Kura value of the parameter.
  auto FromCArgument(llvm::Value* value, const CType& c, const std::string& what) -> llvm::Value* {
    return FromC(value, c, nullptr, what);
  }

  // `extern fn name(...): T => body` is callable from C: a function with C's
  // types that converts the arguments, calls the Kura function, and converts the
  // result back.
  void DefineCExport(FlowGraph* graph) {
    const auto function = graph->GetFunction();
    const auto& c = *function->GetCSignature();
    const auto name = std::string(function->GetName()->GetData());
    const auto symbol = c.symbol;
    if (module_->getFunction(symbol)) {
      Fail("extern fn " + name + ": the C symbol `" + symbol + "` is already used");
      return;
    }
    std::vector<llvm::Type*> param_types{};
    for (const auto& param : c.params)
      param_types.push_back(CAbi(param));
    const auto wrapper = llvm::Function::Create(llvm::FunctionType::get(CAbi(c.result), param_types, false),
                                                llvm::GlobalValue::ExternalLinkage, symbol, module_.get());
    for (size_t idx = 0; idx < c.params.size(); idx++) {
      if (const auto ext = Extension(c.params[idx]); ext != llvm::Attribute::None)
        wrapper->addParamAttr(idx, ext);
    }
    if (const auto ext = Extension(c.result); ext != llvm::Attribute::None)
      wrapper->addRetAttr(ext);
    b_.SetInsertPoint(llvm::BasicBlock::Create(ctx_, "entry", wrapper));

    const auto& kura_params = graph->GetParameterTypes();
    std::vector<llvm::Value*> args{};
    for (size_t idx = 0; idx < c.params.size(); idx++) {
      const auto what = "argument " + std::to_string(idx + 1) + " of `" + symbol + "`";
      args.push_back(Convert(FromCArgument(wrapper->getArg(idx), c.params[idx], what), c.params[idx].ToKuraType(),
                             kura_params[idx]));
    }
    const auto result = b_.CreateCall(natives_[graph], args);
    const auto declared = c.result.ToKuraType();
    const auto converted = Convert(result, graph->GetReturnType(), declared);
    if (c.result.kind == CType::kVoid) {
      b_.CreateRetVoid();
      return;
    }
    b_.CreateRet(ToC(converted, declared, c.result, "the result of `" + symbol + "`"));
  }

  void DefineNative(FlowGraph* graph) {
    if (graph->IsExtern())
      return DefineExternNative(graph);
    FnState state{graph, natives_[graph]};
    fn_ = &state;
    for (const auto block : graph->GetReversePostorder()) {
      if (block->IsGraphEntry())
        continue;
      std::string label = "B" + std::to_string(block->GetBlockId());
      if (block->HasLabel())
        label = std::string(block->GetLabel()->GetData());
      state.blocks[block] = llvm::BasicBlock::Create(ctx_, label, state.native);
    }
    for (const auto block : graph->GetReversePostorder()) {
      if (!block->IsGraphEntry())
        EmitBlock(block);
    }

    // phis last: an incoming value is converted at the end of its predecessor
    for (const auto& [phi, node] : state.phis) {
      for (const auto& in : phi->GetIncoming()) {
        if (graph->IsDead(in.predecessor))
          continue;
        const auto end = state.ends[in.predecessor];
        if (!end || !end->getTerminator()) {
          Fail("internal error: phi in " + graph->GetName() + " has an incoming block with no terminator");
          continue;
        }
        b_.SetInsertPoint(end->getTerminator());
        node->addIncoming(Convert(state.values[in.value->GetDefinition()], Type(in.value), phi->GetCompileType()), end);
      }
    }
    fn_ = nullptr;
  }

  // The thunk takes and returns tagged values; a lambda's captures are read from
  // its closure.
  void DefineThunk(FlowGraph* graph) {
    const auto thunk = thunks_[graph];
    const auto native = natives_[graph];
    b_.SetInsertPoint(llvm::BasicBlock::Create(ctx_, "entry", thunk));
    const auto& types = graph->GetParameterTypes();
    const auto declared = types.size() - graph->GetCaptures().size();

    std::vector<llvm::Value*> args{};
    for (size_t idx = 0; idx < declared; idx++) {
      const auto tag = thunk->getArg(1 + 2 * idx);
      const auto bits = thunk->getArg(2 + 2 * idx);
      args.push_back(Convert(Pack(tag, bits), CompileType::Any(), types[idx]));
    }
    for (size_t idx = 0; idx < graph->GetCaptures().size(); idx++) {
      const auto captured = CallRt("kura_rt_closure_get_capture", value_ty_, {thunk->getArg(0), b_.getInt64(idx)});
      args.push_back(Convert(captured, CompileType::Any(), types[declared + idx]));
    }
    const auto result = b_.CreateCall(native, args);
    b_.CreateRet(Convert(result, graph->GetReturnType(), CompileType::Any()));
  }

  void Declare(FlowGraph* graph) {
    const auto result = graph->GetReturnType();
    if (!result)
      return;  // never analyzed: nothing can call it
    std::vector<llvm::Type*> params{};
    for (const auto type : graph->GetParameterTypes())
      params.push_back(Lower(type));
    const auto name = graph->GetName();
    natives_[graph] = llvm::Function::Create(llvm::FunctionType::get(Lower(result), params, false),
                                             llvm::GlobalValue::InternalLinkage, "kura.fn." + name, module_.get());

    if (graph == graphs_->GetInit())
      return;
    std::vector<llvm::Type*> thunk_params{ptr_ty_};
    const auto declared = graph->GetParameterTypes().size() - graph->GetCaptures().size();
    for (size_t idx = 0; idx < declared; idx++) {
      thunk_params.push_back(i64_);
      thunk_params.push_back(i64_);
    }
    const auto exported = graph->GetFunction() != nullptr && !graph->IsExtern();
    thunks_[graph] =
        llvm::Function::Create(llvm::FunctionType::get(value_ty_, thunk_params, false),
                               exported ? llvm::GlobalValue::ExternalLinkage : llvm::GlobalValue::InternalLinkage,
                               exported ? EntryPointName(name) : "kura.thunk." + name, module_.get());
  }

  // Registers the names and string literals the code uses, then runs the
  // module's top-level bindings.
  void DefineInit() {
    const auto init = llvm::Function::Create(llvm::FunctionType::get(void_ty_, {}, false),
                                             llvm::GlobalValue::ExternalLinkage, "kura_module_init", module_.get());
    b_.SetInsertPoint(llvm::BasicBlock::Create(ctx_, "entry", init));
    for (const auto& name : variants_) {
      CallRt("kura_rt_register_variant", void_ty_, {b_.getInt64(rt::HashName(name)), NameConstant(name)});
    }
    for (const auto& [value, global] : strings_) {
      const auto data = b_.CreateGlobalString(value, "kura.data");
      b_.CreateStore(CallRt("kura_rt_string_new", ptr_ty_, {data, b_.getInt64(value.size())}), global);
    }
    if (graphs_->GetInit() && natives_.contains(graphs_->GetInit()))
      b_.CreateCall(natives_[graphs_->GetInit()], {});
    b_.CreateRetVoid();
  }

 public:
  Generator(llvm::LLVMContext& ctx, ModuleGraph* graphs) :
    ctx_(ctx),
    module_(std::make_unique<llvm::Module>("kura", ctx)),
    b_(ctx),
    graphs_(graphs),
    i64_(llvm::Type::getInt64Ty(ctx)),
    i8_(llvm::Type::getInt8Ty(ctx)),
    i1_(llvm::Type::getInt1Ty(ctx)),
    f64_(llvm::Type::getDoubleTy(ctx)),
    void_ty_(llvm::Type::getVoidTy(ctx)) {
    ptr_ty_ = llvm::PointerType::get(ctx, 0);
    value_ty_ = llvm::StructType::get(ctx, {i64_, i64_});
  }

  auto Generate(std::string* error) -> std::unique_ptr<llvm::Module> {
    for (const auto graph : graphs_->GetGraphs())
      Declare(graph);
    for (const auto graph : graphs_->GetGraphs()) {
      if (natives_.contains(graph)) {
        DefineNative(graph);
        if (thunks_.contains(graph))
          DefineThunk(graph);
        if (graph->GetFunction() && graph->GetFunction()->IsCExport())
          DefineCExport(graph);
      }
    }
    DefineInit();

    if (error_.empty()) {
      std::string problems;
      llvm::raw_string_ostream stream(problems);
      if (llvm::verifyModule(*module_, &stream))
        error_ = "internal error: generated invalid LLVM IR:\n" + problems;
    }
    if (!error_.empty()) {
      if (error)
        *error = error_;
      return nullptr;
    }
    return std::move(module_);
  }
};
}  // namespace

auto GenerateLLVM(llvm::LLVMContext& context, Module* module, ModuleGraph* graphs, std::string* error)
    -> std::unique_ptr<llvm::Module> {
  Generator generator(context, graphs);
  auto result = generator.Generate(error);
  if (!result)
    return nullptr;

  // The C functions that reach C through a shim: compiled together, once.
  std::set<std::string> headers{};
  std::string text{};
  for (const auto function : module->functions) {
    if (!function->IsExtern() || function->GetCSignature()->shim_source.empty())
      continue;
    headers.insert(function->GetCSignature()->header);
    text += function->GetCSignature()->shim_source;
  }
  if (text.empty())
    return result;
  std::string source{};
  for (const auto& header : headers)
    source += "#include \"" + header + "\"\n";
  auto shims = CompileCSource(context, source + text, error);
  if (!shims)
    return nullptr;
  if (result->getDataLayoutStr().empty())
    result->setDataLayout(shims->getDataLayout());
  if (result->getTargetTriple().empty())
    result->setTargetTriple(shims->getTargetTriple());
  if (llvm::Linker::linkModules(*result, std::move(shims))) {
    if (error)
      *error = "can't link the C shims into the module";
    return nullptr;
  }
  return result;
}

auto EmitObjectFile(llvm::Module& module, const std::string& path, std::string* error) -> bool {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  const auto name = llvm::sys::getDefaultTargetTriple();
  // LLVM 21 passes triples around as llvm::Triple rather than strings.
#if LLVM_VERSION_MAJOR >= 21
  const llvm::Triple triple(name);
#else
  const auto& triple = name;
#endif
  std::string problem{};
  const auto target = llvm::TargetRegistry::lookupTarget(triple, problem);
  if (!target) {
    if (error)
      *error = "no target for " + name + ": " + problem;
    return false;
  }
  llvm::TargetOptions options{};
  const auto machine = std::unique_ptr<llvm::TargetMachine>(
      target->createTargetMachine(triple, "generic", "", options, llvm::Reloc::PIC_));
  module.setDataLayout(machine->createDataLayout());
  module.setTargetTriple(triple);
  std::error_code code;
  llvm::raw_fd_ostream out(path, code, llvm::sys::fs::OF_None);
  if (code) {
    if (error)
      *error = "can't write " + path + ": " + code.message();
    return false;
  }
  llvm::legacy::PassManager passes;
  if (machine->addPassesToEmitFile(passes, out, nullptr, llvm::CodeGenFileType::ObjectFile)) {
    if (error)
      *error = "the target can't emit object files";
    return false;
  }
  passes.run(module);
  out.flush();
  return true;
}

void OptimizeLLVM(llvm::Module& module) {
  llvm::LoopAnalysisManager lam;
  llvm::FunctionAnalysisManager fam;
  llvm::CGSCCAnalysisManager cgam;
  llvm::ModuleAnalysisManager mam;
  llvm::PassBuilder pb;
  pb.registerModuleAnalyses(mam);
  pb.registerCGSCCAnalyses(cgam);
  pb.registerFunctionAnalyses(fam);
  pb.registerLoopAnalyses(lam);
  pb.crossRegisterProxies(lam, fam, cgam, mam);
  auto pm = pb.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
  pm.run(module, mam);
}
}  // namespace kura
