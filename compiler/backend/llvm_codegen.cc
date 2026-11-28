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
#include "backend/gir.h"
#include "backend/ir.h"
#include "backend/runtime_abi.h"
#include "backend/swizzle.h"
#include "c_type.h"
#include "color.h"
#include "common.h"
#include "function.h"
#include "heap/objects.h"
#include "local_scope.h"
#include "module.h"
#include "object.h"
#include "property.h"

#define LOWER_FN(Name) void Emit##Name(Name##Instr* instr)

namespace kura {
auto EntryPointName(const std::string& function) -> std::string {
  std::string name = "kura_fn_";
  for (const char c : function)
    name += (c == '-' ? '_' : c);
  return name;
}

namespace {
// kVec is the geometry (Vec2, Vec3, Rect): a vector of 32-bit floats, in a register or on the stack, never tagged.
enum class Rep { kInt, kFloat, kBool, kNone, kPtr, kTagged, kVec, kColor };

auto RepOf(CompileType* type) -> Rep {
  switch (type->GetKind()) {
    case CompileType::kInt:
    case CompileType::kHandle:
      return Rep::kInt;
#define DEFINE_CASE(Name) case CompileType::k##Name:
      FOR_EACH_COLOR_MODEL(DEFINE_CASE)
#undef DEFINE_CASE
      return Rep::kColor;
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
    case CompileType::kNode:
    case CompileType::kSeq:
    case CompileType::kTuple:
    case CompileType::kRecord:
    case CompileType::kFunction:
      return Rep::kPtr;
    case CompileType::kVec2:
    case CompileType::kVec3:
    case CompileType::kRect:
      return Rep::kVec;
    default:
      return Rep::kTagged;
  }
}

// How many floats a geometry type is.
auto LanesOf(CompileType* type) -> unsigned {
  switch (type->GetKind()) {
    case CompileType::kVec2:
      return 2;
    case CompileType::kVec3:
      return 3;
    default:
      return 4;
  }
}

auto TagOf(CompileType* type) -> uint64_t {
  switch (type->GetKind()) {
    case CompileType::kInt:
      return rt::kTagInt;
#define DEFINE_CASE(Name) case CompileType::k##Name:
      FOR_EACH_COLOR_MODEL(DEFINE_CASE)
#undef DEFINE_CASE
      return rt::kTagColor;
    case CompileType::kFloat:
      return rt::kTagFloat;
    case CompileType::kBool:
      return rt::kTagBool;
    case CompileType::kString:
      return rt::kTagString;
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
    case CompileType::kTuple:
      return rt::kTagTuple;
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
    std::unordered_map<EntryInstr*, llvm::BasicBlock*> ends{};
    std::vector<std::pair<PhiInstr*, llvm::PHINode*>> phis{};
    std::unordered_map<Definition*, uint32_t> use_counts{};
  };

  struct ConstBoxInit {
    uint64_t payload_tag;
    uint64_t payload_bits;
    std::string payload_str;
    bool is_string;
    llvm::GlobalVariable* global;
  };
  std::vector<ConstBoxInit> const_boxes_{};
  std::unordered_map<std::string, llvm::GlobalVariable*> const_box_index_{};

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
  llvm::Type* f32_;
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
      case Rep::kColor:
        return llvm::FixedVectorType::get(f32_, 4);
      case Rep::kVec:
        return llvm::FixedVectorType::get(f32_, LanesOf(type));
    }
    return i8_;
  }

  auto ColorPtr(Value* color) -> llvm::Value* {
    llvm::Value* rgba = As(color, CompileType::RgbaColor());
    const auto slot = EntryAlloca(rgba->getType());
    b_.CreateStore(rgba, slot);
    return slot;  // const KuraColor* ({ float r, g, b, a })
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
      case Rep::kVec:
        Fail("internal error: a " + type->ToString() +
             " has no bits to tag: it is converted to a list of floats, not reinterpreted");
        return b_.getInt64(0);
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
      case Rep::kVec:
        Fail("internal error: a " + type->ToString() +
             " is not the bits of a value: it is converted from a list of numbers, not reinterpreted");
        return llvm::PoisonValue::get(Lower(type));
      default:
        return b_.getInt8(0);
    }
  }

  // Converts `value`, which holds a `from`, to hold a `to`. It never branches,
  // so it can be emitted just before the end of a block.
  auto Convert(llvm::Value* value, CompileType* from, CompileType* to) -> llvm::Value* {
    if (from == to)
      return value;

    if (value == nullptr || value->getType()->isVoidTy()) {
      if (RepOf(to) == Rep::kTagged)
        return Pack(b_.getInt64(TagOf(from)), b_.getInt64(0));

      return llvm::PoisonValue::get(Lower(to));
    }

    const auto rf = RepOf(from);
    const auto rt = RepOf(to);
    if (rt == Rep::kVec && (from->IsSeq() || rf == Rep::kTagged)) {
      // A list of numbers is the geometry, if it has the numbers for it: that is checked as it is converted, because
      // a Seq's length isn't in its type.
      llvm::Value* tag = rf == Rep::kTagged ? TagOfValue(value) : b_.getInt64(rt::kTagSeq);
      llvm::Value* bits = rf == Rep::kTagged ? BitsOfValue(value) : b_.CreatePtrToInt(value, i64_);
      const auto vec_ty = Lower(to);
      const auto slot = EntryAlloca(vec_ty);
      CallRt("kura_rt_vec_from", void_ty_, {tag, bits, b_.getInt64(LanesOf(to)), slot});
      return b_.CreateLoad(vec_ty, slot);
    }

    if (rf == Rep::kColor && rt == Rep::kColor) {
      if (from->GetKind() == to->GetKind())
        return value;

      return EmitColorConvert(value, *GetColorModel(*from), *GetColorModel(*to));
    }

    if (rf == Rep::kColor && rt == Rep::kTagged) {
      // Zero-allocation option: convert to Rgba if needed, pack [r,g,b,a] + model into 64-bit payload!
      // Or heap-box via kura_rt_color_new(slot, model) -> rt::kTagColor
      const auto slot = EntryAlloca(value->getType());
      b_.CreateStore(value, slot);
      const auto obj =
          CallRt("kura_rt_color_new", ptr_ty_, {slot, b_.getInt64(static_cast<uint64_t>(*GetColorModel(*from)))});
      return Pack(b_.getInt64(rt::kTagColor), b_.CreatePtrToInt(obj, i64_));
    }

    if (rf == Rep::kTagged && rt == Rep::kColor) {
      CallRt("kura_rt_expect_tag", void_ty_, {TagOfValue(value), b_.getInt64(rt::kTagColor)});
      const auto ptr = b_.CreateIntToPtr(BitsOfValue(value), ptr_ty_);
      const auto slot = EntryAlloca(Lower(to));
      CallRt("kura_rt_color_unbox", void_ty_, {ptr, b_.getInt64(static_cast<uint64_t>(*GetColorModel(*to))), slot});
      return b_.CreateLoad(Lower(to), slot);
    }

    if (rf == Rep::kVec && rt == Rep::kTagged) {
      // geometry as a value of any type is a list of its floats, which is what it can be made from again
      const auto slot = EntryAlloca(value->getType());
      b_.CreateStore(value, slot);
      const auto seq = CallRt("kura_rt_seq_from_vec", ptr_ty_, {slot, b_.getInt64(LanesOf(from))});
      return Pack(b_.getInt64(rt::kTagSeq), b_.CreatePtrToInt(seq, i64_));
    }

    if (rf == Rep::kVec || rt == Rep::kVec) {
      // a Vec2 is only a Vec2: it isn't a number, or another geometry
      Fail("internal error: can't convert " + from->ToString() + " to " + to->ToString());
      return llvm::PoisonValue::get(Lower(to));
    }

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

  static inline auto IsCold(const std::string_view rhs) -> bool {
    return rhs == "kura_rt_destroy" || rhs == "kura_rt_panic" || rhs == "kura_rt_ffi_overflow" ||
           rhs == "kura_rt_ffi_error" || rhs == "kura_rt_test_fail";
  }

  static inline auto IsReadOnly(const std::string_view rhs) -> bool {
    return rhs == "kura_rt_len" || rhs == "kura_rt_string_cmp" || rhs == "kura_rt_record_count" ||
           rhs == "kura_rt_seq_get_ptr" || "kura_rt_seq_get_bits";
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
    auto callee = module_->getOrInsertFunction(name, llvm::FunctionType::get(ret, types, false));
    if (auto* fn = llvm::dyn_cast<llvm::Function>(callee.getCallee())) {
      fn->setDoesNotThrow();
      const std::string_view sv(name);

      if (sv == "kura_rt_panic") {
        fn->setDoesNotReturn();
        fn->addFnAttr(llvm::Attribute::Cold);
      } else {
        fn->setWillReturn();
        if (sv == "kura_rt_destroy" || sv == "kura_rt_panic" || sv == "kura_rt_ffi_overflow" ||
            sv == "kura_rt_ffi_error" || sv == "kura_rt_test_fail" || sv == "kura_rt_test_fail_cmp") {
          fn->addFnAttr(llvm::Attribute::Cold);
        } else if (sv == "kura_rt_len" || sv == "kura_rt_string_cmp" || sv == "kura_rt_record_count" ||
                   sv == "kura_rt_seq_get_ptr" || sv == "kura_rt_seq_get_bits" || sv == "kura_rt_tuple_get_ptr" ||
                   sv == "kura_rt_tuple_get_bits" || sv == "kura_rt_closure_new") {
          fn->setOnlyReadsMemory();
        }
      }
    }
    return b_.CreateCall(callee, flat);
  }

  struct StaticFieldSlot {
    uint64_t offset;
    CompileType* type;
  };

  static auto StaticRecordFieldSlot(CompileType* type, std::string_view field_name) -> std::optional<StaticFieldSlot> {
    if (!type || !type->IsRecord())
      return std::nullopt;
    const auto& fields = type->GetFields();
    for (size_t idx = 0; idx < fields.size(); idx++) {
      if (fields[idx].optional)
        return std::nullopt;
      if (fields[idx].name == field_name) {
        return StaticFieldSlot{16 + idx * 32 + 16, fields[idx].type};
      }
    }
    return std::nullopt;
  }

  // A block of the function's stack as big as a heap object with `payload` bytes after its header. It is made at the
  // function's entry, so it is one block however many times the instruction that wants it is reached.
  auto StackBlock(const uint64_t payload) -> llvm::Value* {
    const uint64_t total =
        (sizeof(heap::HeapObject) + payload + kObjectAlignment - 1) & ~static_cast<uint64_t>(kObjectAlignment - 1);
    auto& entry = fn_->native->getEntryBlock();
    llvm::IRBuilder<> at(&entry, entry.getFirstInsertionPt());
    const auto slot = at.CreateAlloca(llvm::ArrayType::get(llvm::Type::getInt8Ty(ctx_), total));
    slot->setAlignment(llvm::Align(kObjectAlignment));
    return slot;
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

    auto* load = b_.CreateLoad(ptr_ty_, it->second);
    load->setMetadata(llvm::LLVMContext::MD_invariant_load, llvm::MDNode::get(ctx_, {}));
    return load;
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
      std::string defined;
      for (const auto& [def, _] : fn_->values)
        defined += " %" + std::to_string(def->GetId());
      Fail("internal error in `" + fn_->graph->GetName() + "`: %" + std::to_string(value->GetDefinition()->GetId()) +
           " (" + value->GetDefinition()->ToString() + ") is used before it is defined (defined:" + defined + ")");
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

  LOWER_FN(Cast) {
    Value* src = instr->GetValue();
    CompileType* src_ty = Type(src);
    llvm::Value* val = Val(src);
    const TypeId to = instr->GetToType();

    if (to == kBoolType) {
      if (src_ty->IsBool())
        return Define(instr, val, CompileType::Bool());

      if (src_ty->IsNone() || src_ty->IsNever())
        return Define(instr, b_.getInt1(false), CompileType::Bool());

      if (RepOf(src_ty) == Rep::kTagged) {
        llvm::Value* is_some = b_.CreateICmpNE(TagOfValue(val), b_.getInt64(rt::kTagNone), "cast.not_none");
        return Define(instr, is_some, CompileType::Bool());
      }

      return Define(instr, b_.getInt1(true), CompileType::Bool());
    }

    CompileType* dst_ty = instr->GetCompileType() ? instr->GetCompileType() : CompileType::Any();
    Define(instr, Convert(val, src_ty, dst_ty), dst_ty);
  }

  LOWER_FN(Panic) {
    llvm::Value* msg_cstr = nullptr;
    if (const auto c_msg = instr->GetMessage()->GetDefinition()->AsConstant();
        c_msg && c_msg->GetValue()->GetType() == kStringType) {
      msg_cstr = NameConstant(static_cast<String*>(c_msg->GetValue())->GetValue());
    } else {
      msg_cstr = CallRt("kura_rt_string_cstr", ptr_ty_, {As(instr->GetMessage(), CompileType::String())});
    }
    CallRt("kura_rt_panic", void_ty_, {msg_cstr});
  }

  LOWER_FN(Assert) {
    llvm::Function* fn = b_.GetInsertBlock()->getParent();
    auto* pass_bb = llvm::BasicBlock::Create(ctx_, "assert.pass", fn);
    auto* fail_bb = llvm::BasicBlock::Create(ctx_, "assert.fail", fn);

    llvm::Value* cond = As(instr->GetCond(), CompileType::Bool());
    b_.CreateCondBr(cond, pass_bb, fail_bb);

    b_.SetInsertPoint(fail_bb);
    if (instr->HasOperands()) {
      // a comparison: the runtime is given both sides, to say what they were
      CallRt("kura_rt_test_fail_cmp", void_ty_,
             {
                 StringLiteral(std::string(instr->GetFile())),
                 b_.getInt64(instr->GetLine()),
                 b_.getInt64(instr->GetColumn()),
                 b_.getInt64(instr->GetLength()),
                 As(instr->GetMessage(), CompileType::String()),
                 b_.getInt64(static_cast<uint64_t>(instr->GetOp())),
                 As(instr->GetLeft(), CompileType::Any()),
                 As(instr->GetRight(), CompileType::Any()),
             });
    } else {
      CallRt("kura_rt_test_fail", void_ty_,
             {
                 StringLiteral(std::string(instr->GetFile())),
                 b_.getInt64(instr->GetLine()),
                 b_.getInt64(instr->GetColumn()),
                 b_.getInt64(instr->GetLength()),
                 As(instr->GetMessage(), CompileType::String()),
             });
    }

    if (instr->ShouldFailFast()) {
      b_.CreateRet(b_.getInt8(0));  // immediate early return from test function
    } else {
      b_.CreateBr(pass_bb);
    }

    b_.SetInsertPoint(pass_bb);
  }

  LOWER_FN(RegisterEvent) {
    const auto plan = instr->GetPlan().get();
    CallRt("kura_rt_register_event", void_ty_,
           {LayoutTable(plan), b_.getInt64(plan->GetNumberOfNodes()), b_.getInt64(instr->GetNode()),
            b_.getInt64(instr->GetKind()), As(instr->GetMessage(), CompileType::Any())});
  }

  auto EmitRecordFieldLookup(llvm::Value* record_ptr, uint64_t field_hash) -> llvm::Value* {
    llvm::Function* fn = b_.GetInsertBlock()->getParent();
    auto* preheader_bb = b_.GetInsertBlock();
    auto* loop_bb = llvm::BasicBlock::Create(ctx_, "rec.find.loop", fn);
    auto* check_bb = llvm::BasicBlock::Create(ctx_, "rec.find.check", fn);
    auto* found_bb = llvm::BasicBlock::Create(ctx_, "rec.find.found", fn);
    auto* miss_bb = llvm::BasicBlock::Create(ctx_, "rec.find.miss", fn);
    auto* merge_bb = llvm::BasicBlock::Create(ctx_, "rec.find.done", fn);

    // Load `uint32_t count` at byte offset 8 of RecordObj
    llvm::Value* count_ptr = b_.CreateConstInBoundsGEP1_64(i8_, record_ptr, 8);
    llvm::Value* count32 = b_.CreateLoad(llvm::Type::getInt32Ty(ctx_), count_ptr, "rec.count");
    llvm::Value* count = b_.CreateZExt(count32, i64_);
    b_.CreateBr(loop_bb);

    // Loop header: idx = phi [0, preheader], [next_idx, check_bb]
    b_.SetInsertPoint(loop_bb);
    llvm::PHINode* idx = b_.CreatePHI(i64_, 2, "rec.idx");
    idx->addIncoming(b_.getInt64(0), preheader_bb);
    llvm::Value* in_bounds = b_.CreateICmpULT(idx, count, "rec.in_bounds");
    b_.CreateCondBr(in_bounds, check_bb, miss_bb);

    // Check hash at byte offset: 16 + idx * 32
    b_.SetInsertPoint(check_bb);
    llvm::Value* field_offset = b_.CreateAdd(b_.getInt64(16), b_.CreateMul(idx, b_.getInt64(32)));
    llvm::Value* field_ptr = b_.CreateInBoundsGEP(i8_, record_ptr, field_offset, "rec.field");
    llvm::Value* hash = b_.CreateLoad(i64_, field_ptr, "rec.hash");
    llvm::Value* matched = b_.CreateICmpEQ(hash, b_.getInt64(field_hash), "rec.match");
    llvm::Value* next_idx = b_.CreateAdd(idx, b_.getInt64(1), "rec.next_idx");
    idx->addIncoming(next_idx, check_bb);
    b_.CreateCondBr(matched, found_bb, loop_bb);

    // Found: `&field.value` is at `field_ptr + 16`
    b_.SetInsertPoint(found_bb);
    llvm::Value* val_ptr = b_.CreateConstInBoundsGEP1_64(i8_, field_ptr, 16, "rec.val_ptr");
    b_.CreateBr(merge_bb);

    // Miss: return null
    b_.SetInsertPoint(miss_bb);
    b_.CreateBr(merge_bb);

    // Merge
    b_.SetInsertPoint(merge_bb);
    llvm::PHINode* result = b_.CreatePHI(ptr_ty_, 2, "rec.field.val");
    result->addIncoming(val_ptr, found_bb);
    result->addIncoming(llvm::ConstantPointerNull::get(ptr_ty_), miss_bb);
    return result;
  }

  void EmitRecordUpdates(llvm::Value* target_record, CompileType* base_type,
                         const std::vector<UpdateRecordInstr::Entry>& entries) {
    llvm::Function* fn = b_.GetInsertBlock()->getParent();

    for (const auto& entry : entries) {
      const std::string name(entry.property->GetPropertyName()->GetData());
      const uint64_t hash = rt::HashName(name);
      llvm::Value* packed_val = As(entry.value, CompileType::Any());
      CompileType* val_type = Type(entry.value);
      const bool needs_release = !val_type || val_type->IsAny() || val_type->IsString() || val_type->IsSeq() ||
                                 val_type->IsRecord() || val_type->IsVariant() || val_type->IsFunction() ||
                                 val_type->IsNode();

      // Fast path: static slot offset known at compile time -> 0 loops, 0 branches!
      if (const auto slot = StaticRecordFieldSlot(base_type, name)) {
        llvm::Value* field_ptr = b_.CreateConstInBoundsGEP1_64(i8_, target_record, slot->offset, name + ".ptr");
        if (needs_release) {
          llvm::Value* old_val = b_.CreateLoad(value_ty_, field_ptr, "rec.old.val");
          WithTaggedHeapPtr(old_val, [&](llvm::Value* ptr) {
            EmitReleasePtr(ptr);
          });
        }
        b_.CreateStore(packed_val, field_ptr);
        continue;
      }

      // Fallback for records with optional fields
      llvm::Value* field_ptr = EmitRecordFieldLookup(target_record, hash);
      auto* store_bb = llvm::BasicBlock::Create(ctx_, "rec.upd.store", fn);
      auto* fallback_bb = llvm::BasicBlock::Create(ctx_, "rec.upd.set", fn);
      auto* next_bb = llvm::BasicBlock::Create(ctx_, "rec.upd.next", fn);

      b_.CreateCondBr(b_.CreateIsNotNull(field_ptr), store_bb, fallback_bb);

      // Existing field found -> release old value if it may hold a heap pointer, then store
      b_.SetInsertPoint(store_bb);
      if (needs_release) {
        llvm::Value* old_val = b_.CreateLoad(value_ty_, field_ptr, "rec.old.val");
        WithTaggedHeapPtr(old_val, [&](llvm::Value* ptr) {
          EmitReleasePtr(ptr);
        });
      }
      b_.CreateStore(packed_val, field_ptr);
      b_.CreateBr(next_bb);

      // New field (e.g. extending an open record) -> append via kura_rt_record_set
      b_.SetInsertPoint(fallback_bb);
      CallRt("kura_rt_record_set", void_ty_, {target_record, b_.getInt64(hash), NameConstant(name), packed_val});
      b_.CreateBr(next_bb);

      b_.SetInsertPoint(next_bb);
    }
  }

  auto EmitSrgbToLinearVec(llvm::Value* srgba) -> llvm::Value* {
    auto* v4f32 = llvm::FixedVectorType::get(f32_, 4);
    llvm::Value* alpha = b_.CreateExtractElement(srgba, uint64_t{3});

    // Piecewise: c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4)
    llvm::Value* low = b_.CreateFMul(srgba, llvm::ConstantFP::get(v4f32, 1.0f / 12.92f));
    llvm::Value* base = b_.CreateFMul(b_.CreateFAdd(srgba, llvm::ConstantFP::get(v4f32, 0.055f)),
                                      llvm::ConstantFP::get(v4f32, 1.0f / 1.055f));
    llvm::Value* safe_base = b_.CreateMaxNum(base, llvm::ConstantFP::get(v4f32, 0.0f));
    llvm::Value* high =
        b_.CreateIntrinsic(llvm::Intrinsic::pow, {v4f32}, {safe_base, llvm::ConstantFP::get(v4f32, 2.4f)});

    llvm::Value* is_low = b_.CreateFCmpOLE(srgba, llvm::ConstantFP::get(v4f32, 0.04045f));
    llvm::Value* linear = b_.CreateSelect(is_low, low, high);
    return b_.CreateInsertElement(linear, alpha, uint64_t{3});
  }

  auto EmitLinearToSrgbVec(llvm::Value* linear) -> llvm::Value* {
    auto* v4f32 = llvm::FixedVectorType::get(f32_, 4);
    llvm::Value* alpha = b_.CreateExtractElement(linear, uint64_t{3});

    // Piecewise: c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055
    llvm::Value* low = b_.CreateFMul(linear, llvm::ConstantFP::get(v4f32, 12.92f));
    llvm::Value* safe_lin = b_.CreateMaxNum(linear, llvm::ConstantFP::get(v4f32, 0.0f));
    llvm::Value* powered =
        b_.CreateIntrinsic(llvm::Intrinsic::pow, {v4f32}, {safe_lin, llvm::ConstantFP::get(v4f32, 1.0f / 2.4f)});
    llvm::Value* high = b_.CreateFSub(b_.CreateFMul(powered, llvm::ConstantFP::get(v4f32, 1.055f)),
                                      llvm::ConstantFP::get(v4f32, 0.055f));

    llvm::Value* is_low = b_.CreateFCmpOLE(linear, llvm::ConstantFP::get(v4f32, 0.0031308f));
    llvm::Value* srgb = b_.CreateSelect(is_low, low, high);
    return b_.CreateInsertElement(srgb, alpha, uint64_t{3});
  }

  auto EmitColorConvert(llvm::Value* vec, ColorModel from, ColorModel to) -> llvm::Value* {
    if (from == to)
      return vec;

    if (from == ColorModel::kSrgbaColor) {
      llvm::Value* rgba = EmitLinearToSrgbVec(vec);
      return EmitColorConvert(rgba, ColorModel::kRgbaColor, to);
    }
    if (to == ColorModel::kSrgbaColor) {
      llvm::Value* rgba = EmitColorConvert(vec, from, ColorModel::kRgbaColor);
      return EmitSrgbToLinearVec(rgba);
    }

    auto* v4f32 = llvm::FixedVectorType::get(f32_, 4);
    llvm::Value* alpha = b_.CreateExtractElement(vec, uint64_t{3}, "color.a");

    // Direct Hsla <-> Hsba without touching RGB
    if (from == ColorModel::kHslaColor && to == ColorModel::kHsbaColor) {
      llvm::Value* h = b_.CreateExtractElement(vec, uint64_t{0});
      llvm::Value* s = b_.CreateExtractElement(vec, uint64_t{1});
      llvm::Value* l = b_.CreateExtractElement(vec, uint64_t{2});

      llvm::Value* one = llvm::ConstantFP::get(f32_, 1.0f);
      llvm::Value* zero = llvm::ConstantFP::get(f32_, 0.0f);
      llvm::Value* two = llvm::ConstantFP::get(f32_, 2.0f);

      llvm::Value* bv = b_.CreateFAdd(l, b_.CreateFMul(s, b_.CreateMinNum(l, b_.CreateFSub(one, l))));
      llvm::Value* is_zero = b_.CreateFCmpOEQ(bv, zero);
      llvm::Value* sv_nz = b_.CreateFMul(two, b_.CreateFSub(one, b_.CreateFDiv(l, bv)));
      llvm::Value* sv = b_.CreateSelect(is_zero, zero, sv_nz);

      llvm::Value* out = llvm::PoisonValue::get(v4f32);
      out = b_.CreateInsertElement(out, h, uint64_t{0});
      out = b_.CreateInsertElement(out, sv, uint64_t{1});
      out = b_.CreateInsertElement(out, bv, uint64_t{2});
      return b_.CreateInsertElement(out, alpha, uint64_t{3});
    }

    if (from == ColorModel::kHsbaColor && to == ColorModel::kHslaColor) {
      llvm::Value* h = b_.CreateExtractElement(vec, uint64_t{0});
      llvm::Value* s = b_.CreateExtractElement(vec, uint64_t{1});
      llvm::Value* v = b_.CreateExtractElement(vec, uint64_t{2});

      llvm::Value* one = llvm::ConstantFP::get(f32_, 1.0f);
      llvm::Value* zero = llvm::ConstantFP::get(f32_, 0.0f);
      llvm::Value* half = llvm::ConstantFP::get(f32_, 0.5f);

      llvm::Value* l = b_.CreateFMul(v, b_.CreateFSub(one, b_.CreateFMul(s, half)));
      llvm::Value* denom = b_.CreateMinNum(l, b_.CreateFSub(one, l));
      llvm::Value* is_zero = b_.CreateFCmpOEQ(denom, zero);
      llvm::Value* sl_nz = b_.CreateFDiv(b_.CreateFSub(v, l), denom);
      llvm::Value* sl = b_.CreateSelect(is_zero, zero, sl_nz);

      llvm::Value* out = llvm::PoisonValue::get(v4f32);
      out = b_.CreateInsertElement(out, h, uint64_t{0});
      out = b_.CreateInsertElement(out, sl, uint64_t{1});
      out = b_.CreateInsertElement(out, l, uint64_t{2});
      return b_.CreateInsertElement(out, alpha, uint64_t{3});
    }

    // Hsla -> Rgba (branchless SIMD across <4 x float>)
    if (from == ColorModel::kHslaColor && to == ColorModel::kRgbaColor) {
      llvm::Value* h = b_.CreateExtractElement(vec, uint64_t{0});
      llvm::Value* s = b_.CreateExtractElement(vec, uint64_t{1});
      llvm::Value* l = b_.CreateExtractElement(vec, uint64_t{2});

      llvm::Value* one = llvm::ConstantFP::get(f32_, 1.0f);
      llvm::Value* chroma = b_.CreateFMul(s, b_.CreateMinNum(l, b_.CreateFSub(one, l)));
      llvm::Value* h30 = b_.CreateFMul(h, llvm::ConstantFP::get(f32_, 1.0f / 30.0f));

      llvm::Value* vh30 = b_.CreateVectorSplat(4, h30);
      llvm::Value* vl = b_.CreateVectorSplat(4, l);
      llvm::Value* vchroma = b_.CreateVectorSplat(4, chroma);
      llvm::Value* vn = llvm::ConstantDataVector::get(ctx_, std::vector<float>{0.0f, 8.0f, 4.0f, 0.0f});

      llvm::Value* k_raw = b_.CreateFAdd(vn, vh30);
      llvm::Value* vinv12 = llvm::ConstantFP::get(v4f32, 1.0f / 12.0f);
      llvm::Value* v12 = llvm::ConstantFP::get(v4f32, 12.0f);
      llvm::Value* q = b_.CreateIntrinsic(llvm::Intrinsic::floor, {v4f32}, {b_.CreateFMul(k_raw, vinv12)});
      llvm::Value* k = b_.CreateFSub(k_raw, b_.CreateFMul(q, v12));

      llvm::Value* m = b_.CreateMinNum(b_.CreateFSub(k, llvm::ConstantFP::get(v4f32, 3.0f)),
                                       b_.CreateFSub(llvm::ConstantFP::get(v4f32, 9.0f), k));
      m = b_.CreateMaxNum(b_.CreateMinNum(m, llvm::ConstantFP::get(v4f32, 1.0f)), llvm::ConstantFP::get(v4f32, -1.0f));

      llvm::Value* rgb = b_.CreateFSub(vl, b_.CreateFMul(vchroma, m));
      return b_.CreateInsertElement(rgb, alpha, uint64_t{3});
    }

    // Hsba -> Rgba (branchless SIMD across <4 x float>)
    if (from == ColorModel::kHsbaColor && to == ColorModel::kRgbaColor) {
      llvm::Value* h = b_.CreateExtractElement(vec, uint64_t{0});
      llvm::Value* s = b_.CreateExtractElement(vec, uint64_t{1});
      llvm::Value* v = b_.CreateExtractElement(vec, uint64_t{2});

      llvm::Value* chroma = b_.CreateFMul(v, s);
      llvm::Value* h60 = b_.CreateFMul(h, llvm::ConstantFP::get(f32_, 1.0f / 60.0f));

      llvm::Value* vh60 = b_.CreateVectorSplat(4, h60);
      llvm::Value* vv = b_.CreateVectorSplat(4, v);
      llvm::Value* vchroma = b_.CreateVectorSplat(4, chroma);
      llvm::Value* vn = llvm::ConstantDataVector::get(ctx_, std::vector<float>{5.0f, 3.0f, 1.0f, 0.0f});

      llvm::Value* k_raw = b_.CreateFAdd(vn, vh60);
      llvm::Value* vinv6 = llvm::ConstantFP::get(v4f32, 1.0f / 6.0f);
      llvm::Value* v6 = llvm::ConstantFP::get(v4f32, 6.0f);
      llvm::Value* q = b_.CreateIntrinsic(llvm::Intrinsic::floor, {v4f32}, {b_.CreateFMul(k_raw, vinv6)});
      llvm::Value* k = b_.CreateFSub(k_raw, b_.CreateFMul(q, v6));

      llvm::Value* m = b_.CreateMinNum(k, b_.CreateFSub(llvm::ConstantFP::get(v4f32, 4.0f), k));
      m = b_.CreateMaxNum(b_.CreateMinNum(m, llvm::ConstantFP::get(v4f32, 1.0f)), llvm::ConstantFP::get(v4f32, 0.0f));

      llvm::Value* rgb = b_.CreateFSub(vv, b_.CreateFMul(vchroma, m));
      return b_.CreateInsertElement(rgb, alpha, uint64_t{3});
    }

    // Rgba -> Hsla / Hsba
    // (Route Rgba -> Hsba first, then Hsba -> Hsla if target is Hsla)
    llvm::Value* r = b_.CreateExtractElement(vec, uint64_t{0});
    llvm::Value* g = b_.CreateExtractElement(vec, uint64_t{1});
    llvm::Value* b = b_.CreateExtractElement(vec, uint64_t{2});

    llvm::Value* zero = llvm::ConstantFP::get(f32_, 0.0f);
    llvm::Value* max_c = b_.CreateMaxNum(r, b_.CreateMaxNum(g, b));
    llvm::Value* min_c = b_.CreateMinNum(r, b_.CreateMinNum(g, b));
    llvm::Value* delta = b_.CreateFSub(max_c, min_c);

    llvm::Value* no_delta = b_.CreateFCmpOEQ(delta, zero);
    llvm::Value* safe_delta = b_.CreateSelect(no_delta, llvm::ConstantFP::get(f32_, 1.0f), delta);

    llvm::Value* hr = b_.CreateFDiv(b_.CreateFSub(g, b), safe_delta);
    llvm::Value* hg = b_.CreateFAdd(b_.CreateFDiv(b_.CreateFSub(b, r), safe_delta), llvm::ConstantFP::get(f32_, 2.0f));
    llvm::Value* hb = b_.CreateFAdd(b_.CreateFDiv(b_.CreateFSub(r, g), safe_delta), llvm::ConstantFP::get(f32_, 4.0f));

    llvm::Value* is_r = b_.CreateFCmpOEQ(max_c, r);
    llvm::Value* is_g = b_.CreateFCmpOEQ(max_c, g);
    llvm::Value* h_raw = b_.CreateSelect(is_r, hr, b_.CreateSelect(is_g, hg, hb));
    llvm::Value* h_deg = b_.CreateFMul(h_raw, llvm::ConstantFP::get(f32_, 60.0f));
    llvm::Value* h_wrap = b_.CreateSelect(b_.CreateFCmpOLT(h_deg, zero),
                                          b_.CreateFAdd(h_deg, llvm::ConstantFP::get(f32_, 360.0f)), h_deg);
    llvm::Value* h = b_.CreateSelect(no_delta, zero, h_wrap);

    llvm::Value* s_hsba = b_.CreateSelect(b_.CreateFCmpOEQ(max_c, zero), zero, b_.CreateFDiv(delta, max_c));
    llvm::Value* hsba = llvm::PoisonValue::get(v4f32);
    hsba = b_.CreateInsertElement(hsba, h, uint64_t{0});
    hsba = b_.CreateInsertElement(hsba, s_hsba, uint64_t{1});
    hsba = b_.CreateInsertElement(hsba, max_c, uint64_t{2});
    hsba = b_.CreateInsertElement(hsba, alpha, uint64_t{3});

    if (to == ColorModel::kHsbaColor)
      return hsba;
    return EmitColorConvert(hsba, ColorModel::kHsbaColor, ColorModel::kHslaColor);
  }

  auto ToF32(Value* val) -> llvm::Value* {
    llvm::Value* v = Val(val);
    CompileType* ty = Type(val);
    if (ty->IsInt())
      return b_.CreateSIToFP(v, f32_);
    if (RepOf(ty) == Rep::kTagged)
      v = As(val, CompileType::Float());
    return b_.CreateFPTrunc(v, f32_);
  }

  auto ColorChannelToF32(Value* val, const ColorModel model, const uint64_t lane) -> llvm::Value* {
    llvm::Value* v = Val(val);
    CompileType* ty = Type(val);

    if (ty->IsInt()) {
      llvm::Value* f = b_.CreateSIToFP(v, f32_);
      if (model == ColorModel::kRgbaColor && lane < 3)
        return b_.CreateFMul(f, llvm::ConstantFP::get(f32_, 1.0f / 255.0f));
      return f;
    }

    if (ty->IsPct()) {
      llvm::Value* f = b_.CreateFPTrunc(v, f32_);
      return b_.CreateFMul(f, llvm::ConstantFP::get(f32_, 1.0f / 100.0f));
    }

    if (RepOf(ty) == Rep::kTagged)
      v = As(val, CompileType::Float());

    return b_.CreateFPTrunc(v, f32_);
  }

  LOWER_FN(NewColor) {
    const auto model = instr->GetModel();
    auto* vec_ty = llvm::FixedVectorType::get(f32_, 4);
    llvm::Value* vec = llvm::PoisonValue::get(vec_ty);
    vec = b_.CreateInsertElement(vec, ColorChannelToF32(instr->GetC0(), model, 0), uint64_t{0});
    vec = b_.CreateInsertElement(vec, ColorChannelToF32(instr->GetC1(), model, 1), uint64_t{1});
    vec = b_.CreateInsertElement(vec, ColorChannelToF32(instr->GetC2(), model, 2), uint64_t{2});
    vec = b_.CreateInsertElement(vec, ColorChannelToF32(instr->GetAlpha(), model, 3), uint64_t{3});
    Define(instr, vec, CompileType::Color(model));
  }

  LOWER_FN(MathCall) {
    const auto op = instr->GetOp();
    CompileType* ty = instr->GetCompileType();

    auto as_f32_scalar = [&](Value* v) -> llvm::Value* {
      llvm::Value* val = As(v, CompileType::Float());
      return b_.CreateFPTrunc(val, f32_);
    };

    if (op == MathOp::kDot) {
      llvm::Value* a = Val(instr->GetInputAt(0));
      llvm::Value* b = Val(instr->GetInputAt(1));
      llvm::Value* prod = b_.CreateFMul(a, b);
      llvm::Value* sum_f32 = b_.CreateFAddReduce(llvm::ConstantFP::get(f32_, 0.0f), prod);
      return Define(instr, b_.CreateFPExt(sum_f32, f64_), CompileType::Float());
    }

    if (op == MathOp::kLength || op == MathOp::kNormalize) {
      llvm::Value* v = Val(instr->GetInputAt(0));
      llvm::Value* sq = b_.CreateFMul(v, v);
      llvm::Value* len_sq = b_.CreateFAddReduce(llvm::ConstantFP::get(f32_, 0.0f), sq);
      llvm::Value* len = b_.CreateIntrinsic(llvm::Intrinsic::sqrt, {f32_}, {len_sq});
      if (op == MathOp::kLength)
        return Define(instr, b_.CreateFPExt(len, f64_), CompileType::Float());

      auto* vec_ty = llvm::cast<llvm::FixedVectorType>(v->getType());
      llvm::Value* safe_len = b_.CreateMaxNum(len, llvm::ConstantFP::get(f32_, 1e-8f));
      llvm::Value* vlen = b_.CreateVectorSplat(vec_ty->getNumElements(), safe_len);
      return Define(instr, b_.CreateFDiv(v, vlen), ty);
    }

    if (op == MathOp::kClamp) {
      llvm::Value* x = Val(instr->GetInputAt(0));
      llvm::Value* lo = Val(instr->GetInputAt(1));
      llvm::Value* hi = Val(instr->GetInputAt(2));
      if (ty->IsInt()) {
        llvm::Value* c = b_.CreateIntrinsic(llvm::Intrinsic::smax, {i64_}, {x, lo});
        return Define(instr, b_.CreateIntrinsic(llvm::Intrinsic::smin, {i64_}, {c, hi}), ty);
      }
      llvm::Value* c = b_.CreateMaxNum(x, lo);
      return Define(instr, b_.CreateMinNum(c, hi), ty);
    }

    // MathOp::kLerp(a, b, t)
    llvm::Value* a = Val(instr->GetInputAt(0));
    llvm::Value* b = Val(instr->GetInputAt(1));
    Value* t_val = instr->GetInputAt(2);

    // Shortest-path cylindrical hue interpolation for HslaColor and HsbaColor
    if (ty->IsHslaColor() || ty->IsHsbaColor()) {
      llvm::Value* t = as_f32_scalar(t_val);
      llvm::Value* ha = b_.CreateExtractElement(a, uint64_t{0});
      llvm::Value* hb = b_.CreateExtractElement(b, uint64_t{0});

      // dh = ((hb - ha + 540) mod 360) - 180
      llvm::Value* dh_raw = b_.CreateFAdd(b_.CreateFSub(hb, ha), llvm::ConstantFP::get(f32_, 540.0f));
      llvm::Value* q = b_.CreateIntrinsic(llvm::Intrinsic::floor, {f32_},
                                          {b_.CreateFMul(dh_raw, llvm::ConstantFP::get(f32_, 1.0f / 360.0f))});
      llvm::Value* dh = b_.CreateFSub(b_.CreateFSub(dh_raw, b_.CreateFMul(q, llvm::ConstantFP::get(f32_, 360.0f))),
                                      llvm::ConstantFP::get(f32_, 180.0f));
      llvm::Value* h_unwrapped = b_.CreateIntrinsic(llvm::Intrinsic::fmuladd, {f32_}, {dh, t, ha});
      llvm::Value* h_pos = b_.CreateFAdd(h_unwrapped, llvm::ConstantFP::get(f32_, 360.0f));
      llvm::Value* q2 = b_.CreateIntrinsic(llvm::Intrinsic::floor, {f32_},
                                           {b_.CreateFMul(h_pos, llvm::ConstantFP::get(f32_, 1.0f / 360.0f))});
      llvm::Value* h_final = b_.CreateFSub(h_pos, b_.CreateFMul(q2, llvm::ConstantFP::get(f32_, 360.0f)));

      // Interpolate all 4 lanes via FMA, then replace lane 0 with wrapped hue
      auto* v4f32 = llvm::FixedVectorType::get(f32_, 4);
      llvm::Value* vt = b_.CreateVectorSplat(4, t);
      llvm::Value* lerped = b_.CreateIntrinsic(llvm::Intrinsic::fmuladd, {v4f32}, {b_.CreateFSub(b, a), vt, a});
      return Define(instr, b_.CreateInsertElement(lerped, h_final, uint64_t{0}), ty);
    }

    // Vector lerp (<2/3/4 x float>)
    if (auto* vec_ty = llvm::dyn_cast<llvm::FixedVectorType>(a->getType())) {
      llvm::Value* vt = b_.CreateVectorSplat(vec_ty->getNumElements(), as_f32_scalar(t_val));
      llvm::Value* lerped = b_.CreateIntrinsic(llvm::Intrinsic::fmuladd, {vec_ty}, {b_.CreateFSub(b, a), vt, a});
      return Define(instr, lerped, ty);
    }

    // Scalar / Unit lerp (f64)
    llvm::Value* t_f64 = As(t_val, CompileType::Float());
    llvm::Value* lerped = b_.CreateIntrinsic(llvm::Intrinsic::fmuladd, {f64_}, {b_.CreateFSub(b, a), t_f64, a});
    return Define(instr, lerped, ty);
  }

  LOWER_FN(CastColor) {
    Value* src = instr->GetValue();
    CompileType* src_ty = Type(src);
    CompileType* dst_ty = CompileType::Color(instr->GetTarget());
    if (src_ty == dst_ty)
      return Define(instr, Val(src), dst_ty);

    const auto src_model = GetColorModel(*src_ty);
    if (!src_model) {
      // Dynamic `Any` color cast
      llvm::Value* vec = As(src, dst_ty);
      return Define(instr, vec, dst_ty);
    }
    Define(instr, EmitColorConvert(Val(src), *src_model, instr->GetTarget()), dst_ty);
  }

  LOWER_FN(BinaryOp) {
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
      if (op == BinaryOp::kAdd) {
        // Check if this BinaryOp is itself an inner node of a larger single-use string concat chain
        if (fn_->use_counts[instr] == 1) {
          // Verify its single non-ARC user is another String BinaryOp(kAdd) in the same block
          bool absorbed = false;
          for (Instruction* scan = instr->GetNext(); scan != nullptr; scan = scan->GetNext()) {
            if (const auto parent = scan->AsBinaryOp();
                parent && parent->GetOp() == BinaryOp::kAdd && Type(parent->GetLeft())->IsString() &&
                Type(parent->GetRight())->IsString() &&
                (parent->GetLeft()->GetDefinition() == instr || parent->GetRight()->GetDefinition() == instr)) {
              absorbed = true;
              break;
            }
          }
          if (absorbed) {
            // Leave null in `fn_->values[instr]` so any trailing `ReleaseInstr` becomes a compile-time no-op!
            return Define(instr, llvm::ConstantPointerNull::get(ptr_ty_), CompileType::String());
          }
        }

        // Collect all leaf operands of this string concatenation tree left-to-right
        std::vector<Value*> parts{};
        const auto collect = [&](auto& self, Value* v) -> void {
          Definition* def = v->GetDefinition();
          if (const auto inner = def ? def->AsBinaryOp() : nullptr;
              inner && inner->GetOp() == BinaryOp::kAdd && Type(inner->GetLeft())->IsString() &&
              Type(inner->GetRight())->IsString() && fn_->values.contains(inner) &&
              llvm::isa<llvm::ConstantPointerNull>(fn_->values[inner])) {
            self(self, inner->GetLeft());
            self(self, inner->GetRight());
            return;
          }
          parts.push_back(v);
        };
        collect(collect, left);
        collect(collect, right);

        if (parts.size() > 2) {
          auto* arr_ty = llvm::ArrayType::get(ptr_ty_, parts.size());
          llvm::Value* arr_slot = EntryAlloca(arr_ty);
          for (size_t i = 0; i < parts.size(); i++) {
            llvm::Value* gep = b_.CreateConstInBoundsGEP2_64(arr_ty, arr_slot, 0, i);
            b_.CreateStore(Val(parts[i]), gep);
          }
          return Define(instr, CallRt("kura_rt_string_join", ptr_ty_, {arr_slot, b_.getInt64(parts.size())}),
                        CompileType::String());
        }

        return Define(instr, CallRt("kura_rt_string_concat", ptr_ty_, {a, b}), CompileType::String());
      }

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

    auto scalar_to_f32 = [&](Value* v) -> llvm::Value* {
      llvm::Value* f64_val = As(v, CompileType::Float());
      return b_.CreateFPTrunc(f64_val, f32_);
    };

    // 1. Vec2 / Vec3 component-wise +, -, *, /
    if ((ta->IsVec2() && tb->IsVec2()) || (ta->IsVec3() && tb->IsVec3())) {
      llvm::Value* lv = Val(left);
      llvm::Value* rv = Val(right);
      llvm::Value* out = nullptr;
      switch (op) {
        case BinaryOp::kAdd:
          out = b_.CreateFAdd(lv, rv);
          break;
        case BinaryOp::kSubtract:
          out = b_.CreateFSub(lv, rv);
          break;
        case BinaryOp::kMultiply:
          out = b_.CreateFMul(lv, rv);
          break;
        case BinaryOp::kDivide:
          out = b_.CreateFDiv(lv, rv);
          break;
        default:
          break;
      }
      if (out)
        return Define(instr, out, result);
    }

    if ((ta->IsVec2() || ta->IsVec3()) && tb->IsNumeric() && (op == BinaryOp::kMultiply || op == BinaryOp::kDivide)) {
      llvm::Value* lv = Val(left);
      auto* vec_ty = llvm::cast<llvm::FixedVectorType>(lv->getType());
      llvm::Value* vs = b_.CreateVectorSplat(vec_ty->getNumElements(), scalar_to_f32(right));
      llvm::Value* out = op == BinaryOp::kMultiply ? b_.CreateFMul(lv, vs) : b_.CreateFDiv(lv, vs);
      return Define(instr, out, result);
    }
    if (ta->IsNumeric() && (tb->IsVec2() || tb->IsVec3()) && op == BinaryOp::kMultiply) {
      llvm::Value* rv = Val(right);
      auto* vec_ty = llvm::cast<llvm::FixedVectorType>(rv->getType());
      llvm::Value* vs = b_.CreateVectorSplat(vec_ty->getNumElements(), scalar_to_f32(left));
      return Define(instr, b_.CreateFMul(vs, rv), result);
    }

    if (ta->IsRect() && tb->IsVec2() && (op == BinaryOp::kAdd || op == BinaryOp::kSubtract)) {
      llvm::Value* rect_v = Val(left);
      llvm::Value* vec2_v = Val(right);
      llvm::Value* zero2 = llvm::ConstantAggregateZero::get(llvm::FixedVectorType::get(f32_, 2));
      llvm::Value* delta4 = b_.CreateShuffleVector(vec2_v, zero2, {0, 1, 2, 3});
      llvm::Value* out = op == BinaryOp::kAdd ? b_.CreateFAdd(rect_v, delta4) : b_.CreateFSub(rect_v, delta4);
      return Define(instr, out, result);
    }

    if ((ta->IsColor() && tb->IsNumeric() && op == BinaryOp::kMultiply) ||
        (ta->IsNumeric() && tb->IsColor() && op == BinaryOp::kMultiply)) {
      llvm::Value* col_v = ta->IsColor() ? Val(left) : Val(right);
      llvm::Value* scale = ta->IsColor() ? scalar_to_f32(right) : scalar_to_f32(left);
      llvm::Value* alpha = b_.CreateExtractElement(col_v, uint64_t{3});
      llvm::Value* new_a = b_.CreateFMul(alpha, scale);
      return Define(instr, b_.CreateInsertElement(col_v, new_a, uint64_t{3}), result);
    }

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

  LOWER_FN(UnaryOp) {
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

    if (op == UnaryOp::kStringify) {
      if (type->IsString())
        return Define(instr, value, CompileType::String());
      if (type->IsInt())
        return Define(instr, CallRt("kura_rt_int_to_string", ptr_ty_, {value}), CompileType::String());
      if (type->IsFloat())
        return Define(instr, CallRt("kura_rt_float_to_string", ptr_ty_, {value}), CompileType::String());
    }

    Define(instr,
           CallRt("kura_rt_unop", value_ty_,
                  {b_.getInt64(static_cast<uint64_t>(op)), Convert(value, type, CompileType::Any())}),
           CompileType::Any());
  }

  // A zone is a constant source location, laid out as Tracy's ___tracy_source_location_data (three strings, a line
  // and a color), and a call that opens it. Whether anything is recorded is up to the runtime, so a program built
  // with tracing runs the same, and slower only by those two calls, with or without a profiler.
  LOWER_FN(TracyBeginZone) {
    KASSERT(instr);
    const auto& zone = instr->GetZone();
    auto* i32 = llvm::Type::getInt32Ty(ctx_);
    auto* location_ty = llvm::StructType::get(ctx_, {ptr_ty_, ptr_ty_, ptr_ty_, i32, i32});
    auto* name = b_.CreateGlobalString(zone.name, "kura.zone.name");
    auto* function = b_.CreateGlobalString(zone.function, "kura.zone.function");
    auto* file = b_.CreateGlobalString(zone.file, "kura.zone.file");
    auto* initializer = llvm::ConstantStruct::get(
        location_ty,
        {name, function, file, llvm::ConstantInt::get(i32, zone.line), llvm::ConstantInt::get(i32, zone.color)});
    auto* location = new llvm::GlobalVariable(*module_, location_ty, true, llvm::GlobalValue::PrivateLinkage,
                                              initializer, "kura.zone");
    CallRt("kura_rt_tracy_zone_begin", void_ty_, {location});
  }

  LOWER_FN(TracyEndZone) {
    KASSERT(instr);
    CallRt("kura_rt_tracy_zone_end", void_ty_, {});
  }

  LOWER_FN(DrawImage) {
    // TODO(@s0cks): not implement
  }

  LOWER_FN(AllocImage) {
    // TODO(@s0cks): not implement
  }

  LOWER_FN(RectPastClip) {
    llvm::Value* a = Val(instr->GetItem());  // <4 x float>: {x, y, w, h} (or GetLeft())
    llvm::Value* b = Val(instr->GetClip());  // <4 x float>: {x, y, w, h} (or GetRight())

    llvm::Value* ax0 = b_.CreateExtractElement(a, b_.getInt64(0), "a.x0");
    llvm::Value* ay0 = b_.CreateExtractElement(a, b_.getInt64(1), "a.y0");

    llvm::Value* bx0 = b_.CreateExtractElement(b, b_.getInt64(0), "b.x0");
    llvm::Value* by0 = b_.CreateExtractElement(b, b_.getInt64(1), "b.y0");
    llvm::Value* bw = b_.CreateExtractElement(b, b_.getInt64(2), "b.w");
    llvm::Value* bh = b_.CreateExtractElement(b, b_.getInt64(3), "b.h");

    llvm::Value* bx1 = b_.CreateFAdd(bx0, bw, "b.x1");
    llvm::Value* by1 = b_.CreateFAdd(by0, bh, "b.y1");

    // True once the item has scrolled strictly past the right or bottom edge of the clip rect
    llvm::Value* past_x = b_.CreateFCmpOGT(ax0, bx1, "rect.past_x");
    llvm::Value* past_y = b_.CreateFCmpOGT(ay0, by1, "rect.past_y");
    llvm::Value* past = b_.CreateOr(past_x, past_y, "rect.past");

    Define(instr, past, CompileType::Bool());
  }

  LOWER_FN(RectIntersects) {
    llvm::Value* a = Val(instr->GetLeft());   // <4 x float>: {x, y, w, h}
    llvm::Value* b = Val(instr->GetRight());  // <4 x float>: {x, y, w, h}

    llvm::Value* ax0 = b_.CreateExtractElement(a, b_.getInt64(0), "a.x0");
    llvm::Value* ay0 = b_.CreateExtractElement(a, b_.getInt64(1), "a.y0");
    llvm::Value* aw = b_.CreateExtractElement(a, b_.getInt64(2), "a.w");
    llvm::Value* ah = b_.CreateExtractElement(a, b_.getInt64(3), "a.h");
    llvm::Value* ax1 = b_.CreateFAdd(ax0, aw, "a.x1");
    llvm::Value* ay1 = b_.CreateFAdd(ay0, ah, "a.y1");

    llvm::Value* bx0 = b_.CreateExtractElement(b, b_.getInt64(0), "b.x0");
    llvm::Value* by0 = b_.CreateExtractElement(b, b_.getInt64(1), "b.y0");
    llvm::Value* bw = b_.CreateExtractElement(b, b_.getInt64(2), "b.w");
    llvm::Value* bh = b_.CreateExtractElement(b, b_.getInt64(3), "b.h");
    llvm::Value* bx1 = b_.CreateFAdd(bx0, bw, "b.x1");
    llvm::Value* by1 = b_.CreateFAdd(by0, bh, "b.y1");

    // Non-empty AABB intersection: ax0 < bx1 && ax1 > bx0 && ay0 < by1 && ay1 > by0
    llvm::Value* c0 = b_.CreateFCmpOLT(ax0, bx1);
    llvm::Value* c1 = b_.CreateFCmpOGT(ax1, bx0);
    llvm::Value* c2 = b_.CreateFCmpOLT(ay0, by1);
    llvm::Value* c3 = b_.CreateFCmpOGT(ay1, by0);

    llvm::Value* hit = b_.CreateAnd(b_.CreateAnd(c0, c1), b_.CreateAnd(c2, c3), "rect.vis");
    Define(instr, hit, CompileType::Bool());
  }

  LOWER_FN(CheckRendererStatus) {
    CallRt("kura_rt_check_renderer", void_ty_, {});
  }

  inline void EmitRetainPtr(llvm::Value* ptr) {
    llvm::Function* fn = b_.GetInsertBlock()->getParent();
    auto* non_null_bb = llvm::BasicBlock::Create(ctx_, "arc.retain.nn", fn);
    auto* inc_bb = llvm::BasicBlock::Create(ctx_, "arc.retain.inc", fn);
    auto* done_bb = llvm::BasicBlock::Create(ctx_, "arc.retain.done", fn);

    llvm::Value* is_non_null = b_.CreateIsNotNull(ptr, "arc.nn");
    b_.CreateCondBr(is_non_null, non_null_bb, done_bb);

    b_.SetInsertPoint(non_null_bb);
    llvm::Value* header = b_.CreateLoad(i64_, ptr, "arc.hdr");
    llvm::Value* rc = b_.CreateAnd(b_.CreateLShr(header, b_.getInt64(18)), b_.getInt64(0x3FFF), "arc.rc");
    llvm::Value* is_mutable = b_.CreateICmpULT(rc, b_.getInt64(0x3FFF), "arc.mut");
    b_.CreateCondBr(is_mutable, inc_bb, done_bb);

    b_.SetInsertPoint(inc_bb);
    llvm::Value* next_header = b_.CreateAdd(header, b_.getInt64(1ULL << 18), "arc.hdr.inc");
    b_.CreateStore(next_header, ptr);
    b_.CreateBr(done_bb);

    b_.SetInsertPoint(done_bb);
  }

  template <typename Fn>
  inline void WithTaggedHeapPtr(llvm::Value* tagged_val, Fn&& emit_on_ptr) {
    llvm::Value* tag = TagOfValue(tagged_val);
    llvm::Value* bits = BitsOfValue(tagged_val);

    // Variant bit (bit 63) is set:
    llvm::Value* is_variant = b_.CreateICmpSLT(tag, b_.getInt64(0), "tag.is_variant");

    // Or tag is in {5, 6, 10, 11, 12, 13}:
    // Fast bitmask test for tags 0..15: (1 << 5) | (1 << 6) | (1 << 10) | (1 << 11) | (1 << 12) | (1 << 13) = 0x3C60
    constexpr uint64_t kHeapTagMask = (1ULL << rt::kTagString) | (1ULL << rt::kTagColor) | (1ULL << rt::kTagNode) |
                                      (1ULL << rt::kTagSeq) | (1ULL << rt::kTagRecord) | (1ULL << rt::kTagFunction) |
                                      (1ULL << rt::kTagTuple);

    llvm::Value* in_range = b_.CreateICmpULT(tag, b_.getInt64(32), "tag.u32");
    llvm::Value* safe_shift = b_.CreateSelect(in_range, tag, b_.getInt64(0));
    llvm::Value* bit = b_.CreateShl(b_.getInt64(1), safe_shift);
    llvm::Value* is_obj_tag = b_.CreateAnd(
        in_range, b_.CreateICmpNE(b_.CreateAnd(bit, b_.getInt64(kHeapTagMask)), b_.getInt64(0)), "tag.is_obj");

    llvm::Value* may_hold_ptr = b_.CreateOr(is_variant, is_obj_tag, "tag.has_ptr");

    llvm::Function* fn = b_.GetInsertBlock()->getParent();
    auto* ptr_bb = llvm::BasicBlock::Create(ctx_, "arc.tagged.ptr", fn);
    auto* done_bb = llvm::BasicBlock::Create(ctx_, "arc.tagged.done", fn);

    b_.CreateCondBr(may_hold_ptr, ptr_bb, done_bb);

    b_.SetInsertPoint(ptr_bb);
    llvm::Value* ptr = b_.CreateIntToPtr(bits, ptr_ty_, "arc.obj_ptr");
    emit_on_ptr(ptr);
    b_.CreateBr(done_bb);

    b_.SetInsertPoint(done_bb);
  }

  inline void EmitReleasePtr(llvm::Value* ptr) {
    llvm::Function* fn = b_.GetInsertBlock()->getParent();
    auto* non_null_bb = llvm::BasicBlock::Create(ctx_, "arc.rel.nn", fn);
    auto* dec_bb = llvm::BasicBlock::Create(ctx_, "arc.rel.dec", fn);
    auto* destroy_bb = llvm::BasicBlock::Create(ctx_, "arc.rel.destroy", fn);
    auto* done_bb = llvm::BasicBlock::Create(ctx_, "arc.rel.done", fn);

    llvm::Value* is_non_null = b_.CreateIsNotNull(ptr, "arc.nn");
    b_.CreateCondBr(is_non_null, non_null_bb, done_bb);

    b_.SetInsertPoint(non_null_bb);
    llvm::Value* header = b_.CreateLoad(i64_, ptr, "arc.hdr");
    llvm::Value* rc = b_.CreateAnd(b_.CreateLShr(header, b_.getInt64(18)), b_.getInt64(0x3FFF), "arc.rc");
    // Ignore immortal/sticky (0x3FFF) or already-zero headers
    llvm::Value* is_active =
        b_.CreateAnd(b_.CreateICmpNE(rc, b_.getInt64(0)), b_.CreateICmpULT(rc, b_.getInt64(0x3FFF)), "arc.active");
    b_.CreateCondBr(is_active, dec_bb, done_bb);

    b_.SetInsertPoint(dec_bb);
    llvm::Value* next_header = b_.CreateSub(header, b_.getInt64(1ULL << 18), "arc.hdr.dec");
    b_.CreateStore(next_header, ptr);
    llvm::Value* hit_zero = b_.CreateICmpEQ(rc, b_.getInt64(1), "arc.zero");
    b_.CreateCondBr(hit_zero, destroy_bb, done_bb);

    // Cold path: rc was 1 -> now 0. Call cold destructor to release children & reclaim block.
    b_.SetInsertPoint(destroy_bb);
    CallRt("kura_rt_destroy", void_ty_, {ptr});
    b_.CreateBr(done_bb);

    b_.SetInsertPoint(done_bb);
  }

  LOWER_FN(Retain) {
    const auto type = Type(instr->GetValue());
    llvm::Value* val = Val(instr->GetValue());

    if (RepOf(type) == Rep::kTagged) {
      WithTaggedHeapPtr(val, [&](llvm::Value* ptr) {
        EmitRetainPtr(ptr);
      });
    } else if (RepOf(type) == Rep::kPtr) {
      EmitRetainPtr(val);
    }
  }

  LOWER_FN(Release) {
    const auto type = Type(instr->GetValue());
    llvm::Value* val = Val(instr->GetValue());

    if (RepOf(type) == Rep::kTagged) {
      WithTaggedHeapPtr(val, [&](llvm::Value* ptr) {
        EmitReleasePtr(ptr);
      });
    } else if (RepOf(type) == Rep::kPtr) {
      EmitReleasePtr(val);
    }
  }

  LOWER_FN(SeqAppend) {
    const auto seq = ObjectPtr(Val(instr->GetSeq()), Type(instr->GetSeq()));
    const auto item = As(instr->GetItem(), CompileType::Any());
    return Define(instr, CallRt("kura_rt_seq_append", ptr_ty_, {seq, item}), instr->GetCompileType());
  }

  LOWER_FN(UpdateRecord) {
    llvm::Value* base = ObjectPtr(Val(instr->GetBase()), Type(instr->GetBase()));
    CompileType* rec_type = instr->GetCompileType();

    // Fast path available when `InsertARC` proved `base` is at its last use (`move_base == true`)
    if (instr->IsMoveBase()) {
      llvm::Function* fn = b_.GetInsertBlock()->getParent();
      auto* fast_bb = llvm::BasicBlock::Create(ctx_, "rec.upd.inplace", fn);
      auto* slow_bb = llvm::BasicBlock::Create(ctx_, "rec.upd.cow", fn);
      auto* merge_bb = llvm::BasicBlock::Create(ctx_, "rec.upd.done", fn);

      // Load 8-byte ObjectHeader at offset 0 of `base`
      llvm::Value* header = b_.CreateLoad(i64_, base, "rec.header");
      // Extract 14-bit refcount at bits 31..18: (header >> 18) & 0x3FFF
      llvm::Value* shifted = b_.CreateLShr(header, b_.getInt64(18));
      llvm::Value* rc = b_.CreateAnd(shifted, b_.getInt64(0x3FFF), "rec.rc");
      llvm::Value* is_unique = b_.CreateICmpEQ(rc, b_.getInt64(1), "rec.unique");

      b_.CreateCondBr(is_unique, fast_bb, slow_bb);

      // ── Fast Path: refcount == 1 -> mutate `base` in place! ──
      b_.SetInsertPoint(fast_bb);
      EmitRecordUpdates(base, Type(instr->GetBase()), instr->GetEntries());
      b_.CreateBr(merge_bb);
      llvm::BasicBlock* fast_end = b_.GetInsertBlock();

      // ── Slow Path: refcount > 1 -> clone, write updates, release old `base` ──
      b_.SetInsertPoint(slow_bb);
      llvm::Value* cloned = CallRt("kura_rt_record_clone", ptr_ty_, {base});
      EmitRecordUpdates(cloned, Type(instr->GetBase()), instr->GetEntries());
      EmitReleasePtr(base);
      b_.CreateBr(merge_bb);
      llvm::BasicBlock* slow_end = b_.GetInsertBlock();

      // ── Merge ──
      b_.SetInsertPoint(merge_bb);
      llvm::PHINode* phi = b_.CreatePHI(ptr_ty_, 2, "rec.updated");
      phi->addIncoming(base, fast_end);
      phi->addIncoming(cloned, slow_end);
      return Define(instr, phi, rec_type);
    }

    // Non-moving update (`base` is still live elsewhere): always clone
    llvm::Value* cloned = CallRt("kura_rt_record_clone", ptr_ty_, {base});
    EmitRecordUpdates(cloned, Type(instr->GetBase()), instr->GetEntries());
    Define(instr, cloned, rec_type);
  }

  LOWER_FN(BeginFrame) {
    llvm::Value* width = nullptr;
    llvm::Value* height = nullptr;
    if (instr->UsesViewport()) {
      width = CallRt("kura_rt_viewport_width", b_.getInt32Ty(), {});
      height = CallRt("kura_rt_viewport_height", b_.getInt32Ty(), {});
    } else {
      width = b_.getInt32(instr->GetWidth());
      height = b_.getInt32(instr->GetHeight());
    }
    CallRt("kura_rt_begin_frame", void_ty_, {width, height});
  }

  // The constant table of layout nodes of a plan. It's the bytes of `kura::layout::Node`s, which the runtime
  // copies, and its address names the plan's state at run time.
  auto LayoutTable(LayoutPlan* plan) -> llvm::Constant* {
    const auto name = "kura.layout." + std::to_string(plan->GetIndex());
    if (const auto existing = module_->getNamedGlobal(name))
      return existing;
    const auto bytes = plan->GetBytes();
    const auto init = llvm::ConstantDataArray::get(module_->getContext(), llvm::ArrayRef<uint8_t>(bytes));
    const auto global =
        new llvm::GlobalVariable(*module_, init->getType(), true, llvm::GlobalValue::InternalLinkage, init, name);
    global->setAlignment(llvm::Align(alignof(layout::Node)));
    return global;
  }

  LOWER_FN(LayoutSetText) {
    const auto plan = instr->GetPlan().get();
    // The family is a constant of the plan: its bytes, or no pointer at all for the default font.
    const auto& family = instr->GetFontFamily();
    llvm::Value* family_data = family.empty() ? llvm::Constant::getNullValue(ptr_ty_) : NameConstant(family);
    CallRt("kura_rt_layout_set_text", void_ty_,
           {LayoutTable(plan), b_.getInt64(plan->GetNumberOfNodes()), b_.getInt64(instr->GetNode()),
            As(instr->GetText(), CompileType::String()), llvm::ConstantFP::get(f64_, instr->GetFontSize()), family_data,
            b_.getInt64(family.size())});
  }

  LOWER_FN(LayoutSetProperty) {
    const auto plan = instr->GetPlan().get();
    CallRt("kura_rt_layout_set", void_ty_,
           {LayoutTable(plan), b_.getInt64(plan->GetNumberOfNodes()), b_.getInt64(instr->GetNode()),
            b_.getInt64(static_cast<uint64_t>(instr->GetProperty())), As(instr->GetValue(), CompileType::Any())});
  }

  LOWER_FN(LayoutRun) {
    const auto plan = instr->GetPlan().get();
    CallRt("kura_rt_layout_run", void_ty_, {LayoutTable(plan), b_.getInt64(plan->GetNumberOfNodes())});
  }

  LOWER_FN(LayoutSlotBegin) {
    const auto plan = instr->GetPlan().get();
    CallRt("kura_rt_layout_slot_begin", void_ty_,
           {LayoutTable(plan), b_.getInt64(plan->GetNumberOfNodes()), b_.getInt64(instr->GetSlot())});
  }

  LOWER_FN(LayoutSlotInstance) {
    const auto plan = instr->GetPlan().get();
    const auto instance = instr->GetTemplate().get();
    CallRt("kura_rt_layout_slot_instance", void_ty_,
           {LayoutTable(plan), b_.getInt64(plan->GetNumberOfNodes()), b_.getInt64(instr->GetSlot()),
            LayoutTable(instance), b_.getInt64(instance->GetNumberOfNodes())});
  }

  LOWER_FN(LayoutSlotEnd) {
    const auto plan = instr->GetPlan().get();
    CallRt("kura_rt_layout_slot_end", void_ty_,
           {LayoutTable(plan), b_.getInt64(plan->GetNumberOfNodes()), b_.getInt64(instr->GetSlot())});
  }

  // A stack slot, made where the function starts so that a loop doesn't make a new one each time round.
  auto EntryAlloca(llvm::Type* type) -> llvm::Value* {
    // the function being written, whether it is a Kura function or a wrapper that has no `fn_`
    auto& entry = b_.GetInsertBlock()->getParent()->getEntryBlock();
    llvm::IRBuilder<> at_entry(&entry, entry.getFirstInsertionPt());
    return at_entry.CreateAlloca(type);
  }

  // The address of geometry in memory, for the runtime, which takes it as a pointer to the renderer ABI's struct.
  auto Spill(Value* value) -> llvm::Value* {
    const auto vec = Val(value);
    if (!vec->getType()->isVectorTy()) {
      Fail("internal error: a rect must be geometry");
      return llvm::ConstantPointerNull::get(ptr_ty_);
    }
    const auto slot = EntryAlloca(vec->getType());
    b_.CreateStore(vec, slot);
    return slot;
  }

  LOWER_FN(LayoutRect) {
    const auto plan = instr->GetPlan().get();
    const auto rect_ty = llvm::FixedVectorType::get(f32_, 4);
    const auto slot = EntryAlloca(rect_ty);
    CallRt("kura_rt_layout_rect", void_ty_,
           {LayoutTable(plan), b_.getInt64(plan->GetNumberOfNodes()), b_.getInt64(instr->GetNode()), slot});
    Define(instr, b_.CreateLoad(rect_ty, slot), CompileType::Rect());
  }

  LOWER_FN(EndFrame) {
    CallRt("kura_rt_end_frame", void_ty_, {});
  }

  LOWER_FN(DrawRect) {
    CallRt("kura_rt_draw_rect", void_ty_, {});
  }

  inline void EmitDrawVerticalLine(llvm::Value* bounds, llvm::Value* color) {
    NOT_IMPLEMENTED;
  }

  inline void EmitDrawHorizontalLine(llvm::Value* vec, llvm::Value* color) {
    llvm::Value* x = b_.CreateExtractElement(vec, b_.getInt64(0), "x");
    llvm::Value* y = b_.CreateExtractElement(vec, b_.getInt64(1), "y");
    llvm::Value* w = b_.CreateExtractElement(vec, b_.getInt64(2), "w");
    llvm::Value* h = b_.CreateExtractElement(vec, b_.getInt64(3), "h");

    llvm::Value* half = llvm::ConstantFP::get(f32_, 0.5f);
    llvm::Value* x1 = x;
    llvm::Value* y1 = b_.CreateFAdd(y, b_.CreateFMul(h, half));
    llvm::Value* x2 = b_.CreateFAdd(x, w);
    llvm::Value* y2 = y1;

    auto* vec2_ty = llvm::FixedVectorType::get(f32_, 2);
    llvm::Value* from = llvm::UndefValue::get(vec2_ty);
    from = b_.CreateInsertElement(from, x1, b_.getInt64(0));
    from = b_.CreateInsertElement(from, y1, b_.getInt64(1));

    llvm::Value* to = llvm::UndefValue::get(vec2_ty);
    to = b_.CreateInsertElement(to, x2, b_.getInt64(0));
    to = b_.CreateInsertElement(to, y2, b_.getInt64(1));

    llvm::Value* start_ptr = EntryAlloca(vec2_ty);
    b_.CreateStore(from, start_ptr);

    llvm::Value* end_ptr = EntryAlloca(vec2_ty);
    b_.CreateStore(to, end_ptr);

    CallRt("kura_rt_draw_line", void_ty_, {start_ptr, end_ptr, color});
  }

  LOWER_FN(DrawLine) {
    const auto bounds = Val(instr->GetBounds());
    const auto color = ColorPtr(instr->GetColor());
    switch (instr->GetDirection()) {
      case DrawLineInstr::kHorizontal:
        return EmitDrawHorizontalLine(bounds, color);
      case DrawLineInstr::kVertical:
        return EmitDrawVerticalLine(bounds, color);
    }
  }

  // kura_rt_fill_rect takes the rect as the bits of f64s, whichever number type the operand is.
  auto CoordinateBits(Value* value) -> llvm::Value* {
    auto v = Val(value);
    if (v->getType()->isIntegerTy(64)) {
      v = b_.CreateSIToFP(v, f64_);
    } else if (!v->getType()->isDoubleTy()) {
      Fail("internal error: a rect coordinate must be a number");
      return b_.getInt64(0);
    }
    return b_.CreateBitCast(v, i64_);
  }

  LOWER_FN(FillRect) {
    CallRt("kura_rt_fill_rect", void_ty_, {Spill(instr->GetRect()), ColorPtr(instr->GetColor())});
  }

  LOWER_FN(PaintBox) {
    CallRt("kura_rt_paint_box", void_ty_,
           {Spill(instr->GetRect()), ColorPtr(instr->GetFill()), ColorPtr(instr->GetBorderColor()),
            CoordinateBits(instr->GetBorderWidth()), CoordinateBits(instr->GetRadius()),
            CoordinateBits(instr->GetOpacity())});
  }

  LOWER_FN(PushClip) {
    CallRt("kura_rt_push_clip", void_ty_, {Spill(instr->GetRect())});
  }

  void EmitPopClip(PopClipInstr*) {
    CallRt("kura_rt_pop_clip", void_ty_, {});
  }

  void EmitDrawText(DrawTextInstr* draw) {
    // What's drawn, and where, is the layout's: it has the node's text, box and font.
    const auto plan = draw->GetPlan().get();
    CallRt("kura_rt_draw_text", void_ty_,
           {LayoutTable(plan), b_.getInt64(plan->GetNumberOfNodes()), b_.getInt64(draw->GetNode()),
            ColorPtr(draw->GetColor()), CoordinateBits(draw->GetOpacity()), b_.getInt64(draw->GetAlign())});
  }

  void EmitDrawInput(DrawInputInstr* draw) {
    // The text, the box and the font are the layout's. The placeholder, the message and readonly are tagged
    // values: the runtime reads what each is by its tag.
    const auto plan = draw->GetPlan().get();
    CallRt("kura_rt_draw_input", void_ty_,
           {LayoutTable(plan), b_.getInt64(plan->GetNumberOfNodes()), b_.getInt64(draw->GetNode()),
            ColorPtr(draw->GetColor()), CoordinateBits(draw->GetOpacity()), b_.getInt64(draw->GetAlign()),
            As(draw->GetPlaceholder(), CompileType::Any()), As(draw->GetMessage(), CompileType::Any()),
            As(draw->GetReadonly(), CompileType::Any())});
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

  // `init`, `update` and `view` are run in a view, which they are given: as a hidden last parameter, after the ones
  // that are written, so that what the graph says the parameters are is still where it was. Kura code can't name it
  // and takes no parameter for it. The host's call gives it (the thunk, which runs the function in it), and a call
  // from Kura code passes on the one it has, or none if it is code that wasn't given one, a test, which then stays in
  // the view it is in.
  auto HasThis(FlowGraph* graph) const -> bool {
    if (graph == graphs_->GetInit() || graph->IsExtern())
      return false;
    const auto name = graph->GetName();
    return name == "init" || name == "update" || name == "view";
  }

  void AppendThis(FlowGraph* callee, std::vector<llvm::Value*>* args) {
    if (!HasThis(callee))
      return;
    if (fn_ != nullptr && HasThis(fn_->graph))
      args->push_back(fn_->native->getArg(fn_->native->arg_size() - 1));
    else
      args->push_back(llvm::ConstantPointerNull::get(ptr_ty_));
  }

  void EmitCall(CallInstr* call) {
    const auto& args = call->GetArgs();
    const auto target = call->GetTarget();

    if (const auto c_target = target->GetDefinition()->AsConstant();
        c_target && c_target->GetValue()->GetType() == kStringType &&
        static_cast<String*>(c_target->GetValue())->GetValue() == "panic") {
      llvm::Value* msg_cstr = nullptr;
      if (!args.empty()) {
        if (const auto c_msg = args[0]->GetDefinition()->AsConstant();
            c_msg && c_msg->GetValue()->GetType() == kStringType) {
          msg_cstr = NameConstant(static_cast<String*>(c_msg->GetValue())->GetValue());
        } else {
          msg_cstr = CallRt("kura_rt_string_cstr", ptr_ty_, {As(args[0], CompileType::String())});
        }
      } else {
        msg_cstr = NameConstant("panic");
      }

      CallRt("kura_rt_panic", void_ty_, {msg_cstr});
      b_.CreateUnreachable();
      return;
    }

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
        AppendThis(graph, &values);
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
    const auto type = value->GetType();
    if (type == BuiltinTypes::kIntType)
      return Define(instr, b_.getInt64(static_cast<Int*>(value)->GetValue()), CompileType::Int());

#define DEFINE_COLOR_CONST(Name)                                                                                    \
  if (type == BuiltinTypes::k##Name##Type) {                                                                        \
    const auto* c = static_cast<Name*>(value);                                                                      \
    const auto& s = c->GetStorage();                                                                                \
    return Define(                                                                                                  \
        instr,                                                                                                      \
        llvm::ConstantDataVector::get(ctx_, std::vector<float>{static_cast<float>(s.c0), static_cast<float>(s.c1),  \
                                                               static_cast<float>(s.c2), static_cast<float>(s.a)}), \
        CompileType::Name());                                                                                       \
  }
    FOR_EACH_COLOR_MODEL(DEFINE_COLOR_CONST)
#undef DEFINE_COLOR_CONST

    if (type == BuiltinTypes::kVec2Type) {
      const auto& v = static_cast<Vec2*>(value)->GetValue();
      return Define(instr, llvm::ConstantDataVector::get(ctx_, std::vector<float>{v.x, v.y}), CompileType::Vec2());
    }
    if (type == BuiltinTypes::kVec3Type) {
      const auto& v = static_cast<Vec3*>(value)->GetValue();
      return Define(instr, llvm::ConstantDataVector::get(ctx_, std::vector<float>{v.x, v.y, v.z}), CompileType::Vec3());
    }
    if (type == BuiltinTypes::kRectType) {
      const auto& r = static_cast<Rect*>(value)->GetValue();
      return Define(instr, llvm::ConstantDataVector::get(ctx_, std::vector<float>{r.x, r.y, r.width, r.height}),
                    CompileType::Rect());
    }
    if (type == BuiltinTypes::kFloatType)
      return Define(instr, llvm::ConstantFP::get(f64_, static_cast<Float*>(value)->GetValue()), CompileType::Float());
    if (type == BuiltinTypes::kPxType)
      return Define(instr, llvm::ConstantFP::get(f64_, static_cast<Px*>(value)->GetValue()), CompileType::Px());
    if (type == BuiltinTypes::kPctType)
      return Define(instr, llvm::ConstantFP::get(f64_, static_cast<Pct*>(value)->GetValue()), CompileType::Pct());
    if (type == BuiltinTypes::kFrType)
      return Define(instr, llvm::ConstantFP::get(f64_, static_cast<Fr*>(value)->GetValue()), CompileType::Fr());
    if (type == BuiltinTypes::kBoolType)
      return Define(instr, b_.getInt1(static_cast<Bool*>(value)->GetValue()), CompileType::Bool());
    if (type == BuiltinTypes::kNoneType)
      return Define(instr, b_.getInt8(0), CompileType::None());
    if (type == BuiltinTypes::kStringType)
      return Define(instr, StringLiteral(static_cast<String*>(value)->GetValue()), CompileType::String());

    Fail("internal error: can't generate a constant of " + value->ToString());
  }

  LOWER_FN(NewTuple) {
    const uint64_t total_count = instr->GetCompileType()->GetMembers().size();
    llvm::Value* tuple = nullptr;
    if (instr->IsStackAllocated()) {
      tuple = StackBlock(heap::TupleObj::PayloadSize(total_count));
      CallRt("kura_rt_stack_tuple", void_ty_, {tuple, b_.getInt64(total_count)});
    } else {
      tuple = CallRt("kura_rt_tuple_new", ptr_ty_, {b_.getInt64(total_count)});
    }

    uint64_t out_index = 0;
    for (uint64_t i = 0; i < instr->GetNumberOfInputs(); i++) {
      Value* input = instr->GetInputAt(i);
      if (instr->IsSpreadAt(i)) {
        CompileType* spread_ty = Type(input);
        llvm::Value* src_tuple = ObjectPtr(Val(input), spread_ty);
        const auto& inner_elems = spread_ty->GetMembers();
        for (size_t j = 0; j < inner_elems.size(); j++) {
          llvm::Value* item = CallRt("kura_rt_tuple_get", value_ty_, {src_tuple, b_.getInt64(j)});
          CallRt("kura_rt_tuple_set", void_ty_, {tuple, b_.getInt64(out_index++), item});
        }
      } else {
        CallRt("kura_rt_tuple_set", void_ty_, {tuple, b_.getInt64(out_index++), As(input, CompileType::Any())});
      }
    }

    return Define(instr, tuple, instr->GetCompileType());
  }

  void EmitInstruction(Instruction* instr) {
#define CHECK(Name)                     \
  if (const auto i = instr->As##Name()) \
    return Emit##Name(i);

    if (const auto p = instr->AsParameter())
      return Define(p, fn_->native->getArg(p->GetIndex()), p->GetCompileType());

    CHECK(Constant);
    CHECK(BinaryOp);
    CHECK(UnaryOp);
    CHECK(Call);
    CHECK(Panic);
    CHECK(Cast);
    CHECK(DrawRect);
    CHECK(FillRect);
    CHECK(PaintBox);
    CHECK(DrawLine);
    CHECK(PushClip);
    CHECK(PopClip);
    CHECK(DrawText);
    CHECK(RegisterEvent);
    CHECK(DrawInput);
    CHECK(BeginFrame);
    CHECK(EndFrame);
    CHECK(LayoutSetText);
    CHECK(LayoutSetProperty);
    CHECK(LayoutRun);
    CHECK(LayoutSlotBegin);
    CHECK(LayoutSlotInstance);
    CHECK(LayoutSlotEnd);
    CHECK(LayoutRect);
    CHECK(CheckRendererStatus);
    CHECK(TracyBeginZone);
    CHECK(TracyEndZone);
    CHECK(RectIntersects);
    CHECK(RectPastClip);
    CHECK(Retain);
    CHECK(Release);
    CHECK(SeqAppend);
    CHECK(UpdateRecord);
    CHECK(NewColor);
    CHECK(CastColor);
    CHECK(MathCall);
    CHECK(Assert);
    CHECK(NewTuple);

#undef CHECK

    if (const auto phi = instr->AsPhi()) {
      const auto node = b_.CreatePHI(Lower(phi->GetCompileType()), phi->GetNumberOfIncoming());
      fn_->values[phi] = node;
      fn_->phis.push_back({phi, node});
      return;
    }

    if (const auto l = instr->AsLoadFunction()) {
      const auto graph = graphs_->GetGraphFor(l->GetFunction());
      const auto thunk = graph ? thunks_[graph] : nullptr;
      // a closure's thunk is given the closure where an entry point's is given the view
      if (graph && HasThis(graph))
        return Fail("`" + graph->GetName() + "` is run by the host, in its view: it can't be used as a value");
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
      llvm::Value* closure = nullptr;
      if (n->IsStackAllocated()) {
        closure = StackBlock(heap::ClosureObj::PayloadSize(n->GetCaptures().size()));
        CallRt("kura_rt_stack_closure", void_ty_,
               {closure, thunk, b_.getInt64(arity), b_.getInt64(n->GetCaptures().size())});
      } else {
        closure =
            CallRt("kura_rt_closure_new", ptr_ty_, {thunk, b_.getInt64(arity), b_.getInt64(n->GetCaptures().size())});
      }
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
      llvm::Value* seq = nullptr;
      if (l->IsStackAllocated()) {
        seq = StackBlock(heap::SeqObj::PayloadSize(l->GetInputs().size()));
        CallRt("kura_rt_stack_seq", void_ty_, {seq, b_.getInt64(l->GetInputs().size())});
      } else {
        seq = CallRt("kura_rt_seq_new", ptr_ty_, {b_.getInt64(l->GetInputs().size())});
      }
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
      const auto inst_ty = Type(l->GetInstance());
      const auto object = ObjectPtr(Val(l->GetInstance()), inst_ty);
      const auto index = As(l->GetIndex(), CompileType::Int());
      CompileType* elem_ty = l->GetCompileType();

      if (inst_ty->IsTuple() && elem_ty) {
        const Rep elem_rep = RepOf(elem_ty);
        if (elem_rep == Rep::kPtr)
          return Define(l, CallRt("kura_rt_tuple_get_ptr", ptr_ty_, {object, index}), elem_ty);
        if (elem_rep == Rep::kInt || elem_rep == Rep::kFloat || elem_rep == Rep::kBool)
          return Define(l, FromBits(CallRt("kura_rt_tuple_get_bits", i64_, {object, index}), elem_ty), elem_ty);
        if (elem_rep == Rep::kNone)
          return Define(l, b_.getInt8(0), elem_ty);
        return Define(l, CallRt("kura_rt_tuple_get", value_ty_, {object, index}), elem_ty);
      }
      if (inst_ty->IsSeq() && elem_ty && RepOf(elem_ty) != Rep::kTagged && RepOf(elem_ty) != Rep::kNone) {
        if (RepOf(elem_ty) == Rep::kPtr)
          return Define(l, CallRt("kura_rt_seq_get_ptr", ptr_ty_, {object, index}), elem_ty);
        if (RepOf(elem_ty) == Rep::kInt || RepOf(elem_ty) == Rep::kFloat || RepOf(elem_ty) == Rep::kBool)
          return Define(l, FromBits(CallRt("kura_rt_seq_get_bits", i64_, {object, index}), elem_ty), elem_ty);
      }

      return Define(l, CallRt("kura_rt_seq_get", value_ty_, {object, index}), CompileType::Any());
    }

    if (const auto l = instr->AsLoadLength()) {
      const auto object = ObjectPtr(Val(l->GetInstance()), Type(l->GetInstance()));
      return Define(l, CallRt("kura_rt_len", i64_, {object}), CompileType::Int());
    }

    if (const auto r = instr->AsNewRecord()) {
      if (r->IsStackAllocated()) {
        const auto record = StackBlock(heap::RecordObj::PayloadSize(static_cast<uint32_t>(r->GetProperties())));
        CallRt("kura_rt_stack_record", void_ty_, {record, b_.getInt64(r->GetProperties())});
        return Define(r, record, r->GetCompileType());
      }
      // its room: the entries it is given, and as many fields as each record that is spread into it has
      llvm::Value* capacity = b_.getInt64(r->GetProperties());
      for (const auto spread : r->GetInputs()) {
        const auto count = CallRt("kura_rt_record_count", i64_, {ObjectPtr(Val(spread), Type(spread))});
        capacity = b_.CreateAdd(capacity, count);
      }
      return Define(r, CallRt("kura_rt_record_new", ptr_ty_, {capacity}), r->GetCompileType());
    }
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
#define DEFINE_CASE(Name, Tag)      \
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
      if (v->HasPayload()) {
        if (const auto c = v->GetPayload()->GetDefinition()->AsConstant()) {
          Object* obj = c->GetValue();
          uint64_t p_tag = rt::kTagNone;
          uint64_t p_bits = 0;
          std::string p_str{};
          bool is_str = false;
          bool hoistable = true;

          switch (obj->GetType()) {
            case kIntType:
              p_tag = rt::kTagInt;
              p_bits = static_cast<uint64_t>(static_cast<Int*>(obj)->GetValue());
              break;
            case kFloatType:
              p_tag = rt::kTagFloat;
              p_bits = std::bit_cast<uint64_t>(static_cast<Float*>(obj)->GetValue());
              break;
            case kPxType:
              p_tag = rt::kTagPx;
              p_bits = std::bit_cast<uint64_t>(static_cast<Px*>(obj)->GetValue());
              break;
            case kPctType:
              p_tag = rt::kTagPct;
              p_bits = std::bit_cast<uint64_t>(static_cast<Pct*>(obj)->GetValue());
              break;
            case kFrType:
              p_tag = rt::kTagFr;
              p_bits = std::bit_cast<uint64_t>(static_cast<Fr*>(obj)->GetValue());
              break;
            case kBoolType:
              p_tag = rt::kTagBool;
              p_bits = static_cast<Bool*>(obj)->GetValue() ? 1 : 0;
              break;
            case kNoneType:
              p_tag = rt::kTagNone;
              p_bits = 0;
              break;
            case kStringType:
              p_tag = rt::kTagString;
              p_str = static_cast<String*>(obj)->GetValue();
              is_str = true;
              break;
            default:
              hoistable = false;
              break;
          }

          if (hoistable) {
            const std::string key =
                is_str ? ("S:" + p_str) : ("B:" + std::to_string(p_tag) + ":" + std::to_string(p_bits));
            auto it = const_box_index_.find(key);
            if (it == const_box_index_.end()) {
              auto* global = new llvm::GlobalVariable(*module_, ptr_ty_, false, llvm::GlobalValue::InternalLinkage,
                                                      llvm::ConstantPointerNull::get(ptr_ty_),
                                                      "kura.box." + std::to_string(const_boxes_.size()));
              const_boxes_.push_back(ConstBoxInit{p_tag, p_bits, std::move(p_str), is_str, global});
              it = const_box_index_.emplace(key, global).first;
            }
            auto* load = b_.CreateLoad(ptr_ty_, it->second);
            load->setMetadata(llvm::LLVMContext::MD_invariant_load, llvm::MDNode::get(ctx_, {}));
            box = load;
            return Define(v, Pack(b_.getInt64(rt::VariantTag(v->GetVariantName())), b_.CreatePtrToInt(box, i64_)),
                          CompileType::Any());
          }
        }

        if (v->IsStackAllocated()) {
          box = StackBlock(heap::BoxObj::PayloadSize());
          CallRt("kura_rt_stack_box", void_ty_, {box, As(v->GetPayload(), CompileType::Any())});
        } else {
          box = CallRt("kura_rt_box", ptr_ty_, {As(v->GetPayload(), CompileType::Any())});
        }
      }
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
    const auto name = std::string(property->GetPropertyName()->GetData());
    const uint64_t hash = rt::HashName(name);

    if (!safe && RepOf(type) == Rep::kPtr && type->IsRecord()) {
      if (const auto slot = StaticRecordFieldSlot(type, name)) {
        const auto load_instr = def->AsLoadProperty();
        const bool is_move = load_instr && load_instr->IsMove();
        const Rep field_rep = RepOf(slot->type);

        if (field_rep == Rep::kInt || field_rep == Rep::kFloat || field_rep == Rep::kBool || field_rep == Rep::kPtr) {
          llvm::Value* bits_ptr = b_.CreateConstInBoundsGEP1_64(i8_, value, slot->offset + 8, name + ".bits.ptr");
          llvm::Value* bits = b_.CreateLoad(i64_, bits_ptr, name + ".bits");
          if (is_move) {
            llvm::Value* slot_ptr = b_.CreateConstInBoundsGEP1_64(i8_, value, slot->offset, name + ".slot.ptr");
            b_.CreateStore(Pack(b_.getInt64(rt::kTagNone), b_.getInt64(0)), slot_ptr);
          }
          return Define(def, FromBits(bits, slot->type), slot->type);
        }

        llvm::Value* field_ptr = b_.CreateConstInBoundsGEP1_64(i8_, value, slot->offset, name + ".ptr");
        llvm::Value* loaded = b_.CreateLoad(value_ty_, field_ptr, name + ".val");
        if (is_move) {
          b_.CreateStore(Pack(b_.getInt64(rt::kTagNone), b_.getInt64(0)), field_ptr);
        }
        return Define(def, loaded, slot->type);
      }
    }

    if (const auto lane = VectorPropertyLane(type, name)) {
      llvm::Value* elem_f32 = b_.CreateExtractElement(value, static_cast<uint64_t>(*lane), name);
      llvm::Value* elem_f64 = b_.CreateFPExt(elem_f32, f64_);
      return Define(def, elem_f64, CompileType::Float());
    }

    if (const auto swizzle = ParseSwizzle(type, name)) {
      llvm::Value* shuffled = b_.CreateShuffleVector(value, value, swizzle->indices, name);
      CompileType* out_ty = swizzle->indices.size() == 2 ? CompileType::Vec2()
                          : swizzle->indices.size() == 3 ? CompileType::Vec3()
                                                         : (swizzle->is_color ? type : CompileType::Rect());
      return Define(def, shuffled, out_ty);
    }

    // Fallback for tagged / Node properties
    if (RepOf(type) == Rep::kTagged) {
      return Define(def, CallRt(safe ? "kura_rt_get_safe" : "kura_rt_get_dyn", value_ty_, {value, b_.getInt64(hash)}),
                    CompileType::Any());
    }
    Define(def, CallRt("kura_rt_record_get", value_ty_, {value, b_.getInt64(hash)}), CompileType::Any());
  }

  // ── blocks and functions ─────────────────────────────────────────────────
  void EmitBlock(EntryInstr* block) {
    const auto bb = fn_->blocks[block];
    b_.SetInsertPoint(bb);
    if (fn_->graph->IsDead(block) && block != fn_->graph->GetEntry()->GetTarget()) {
      b_.CreateUnreachable();
      fn_->ends[block] = bb;
      return;
    }

    if (const auto join = block->AsJoinEntry()) {
      for (const auto phi : join->GetPhis()) {
        const auto node = b_.CreatePHI(Lower(phi->GetCompileType()), phi->GetNumberOfIncoming());
        fn_->values[phi] = node;
        fn_->phis.push_back({phi, node});
      }
    }

    for (Instruction* instr = block->GetNext(); instr != nullptr; instr = instr->GetNext()) {
      if (instr->IsPhi())
        continue;
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
        if (ret->HasValue()) {
          const auto value = As(ret->GetValue(), fn_->graph->GetReturnType());
          fn_->ends[block] = b_.GetInsertBlock();
          b_.CreateRet(value);
        } else {
          fn_->ends[block] = b_.GetInsertBlock();
          b_.CreateRetVoid();
        }
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
    const auto record = CallRt("kura_rt_record_new", ptr_ty_, {b_.getInt64(rec.fields.size())});
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
    AppendThis(target, &args);
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

    // the result, and every parameter that is an out
    const auto record = CallRt("kura_rt_record_new", ptr_ty_, {b_.getInt64(c.params.size() + 1)});
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
    AppendThis(graph, &args);
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
      if (graph->IsDead(block))
        continue;
      if (const auto join = block->AsJoinEntry()) {
        for (const auto phi : join->GetPhis()) {
          for (const auto& in : phi->GetIncoming()) {
            if (in.value && in.value->GetDefinition())
              state.use_counts[in.value->GetDefinition()]++;
          }
        }
      }
      for (Instruction* instr = block->GetNext(); instr != nullptr; instr = instr->GetNext()) {
        if (instr->IsPhi() || instr->IsRelease() || instr->IsRetain())
          continue;
        for (uint64_t i = 0; i < instr->GetNumberOfInputs(); i++) {
          if (const auto in = instr->GetInputAt(i); in && in->GetDefinition())
            state.use_counts[in->GetDefinition()]++;
        }
      }
    }

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

        auto defInstr = in.value->GetDefinition();
        if (defInstr && defInstr->AsReturn()) {
          node->addIncoming(llvm::UndefValue::get(Lower(phi->GetCompileType())), end);
          continue;
        }

        llvm::Value* llvmVal = state.values[defInstr];
        if (llvmVal == nullptr || llvmVal->getType()->isVoidTy()) {
          node->addIncoming(llvm::UndefValue::get(Lower(phi->GetCompileType())), end);
          continue;
        }

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
    // The host's call into the program: the first argument is the view the program runs in (where a closure's thunk
    // is given the closure), and the function runs inside it.
    if (HasThis(graph)) {
      CallRt("kura_rt_view_enter", void_ty_, {thunk->getArg(0)});
      args.push_back(thunk->getArg(0));
    }
    const auto result = b_.CreateCall(native, args);
    if (HasThis(graph))
      CallRt("kura_rt_view_leave", void_ty_, {});
    b_.CreateRet(Convert(result, graph->GetReturnType(), CompileType::Any()));
  }

  void Declare(FlowGraph* graph) {
    const auto result = graph->GetReturnType();
    if (!result)
      return;  // never analyzed: nothing can call it

    std::vector<llvm::Type*> params{};
    for (const auto type : graph->GetParameterTypes())
      params.push_back(Lower(type));
    if (HasThis(graph))
      params.push_back(ptr_ty_);  // the view
    llvm::FunctionType* retType = nullptr;
    if (graph->GetName() == "view") {
      retType = llvm::FunctionType::get(void_ty_, params, false);
    } else {
      retType = llvm::FunctionType::get(Lower(result), params, false);
    }

    const auto name = graph->GetName();
    auto* native =
        llvm::Function::Create(retType, llvm::GlobalValue::InternalLinkage, "kura.fn." + name, module_.get());
    natives_[graph] = native;
    if (graph->IsPure()) {
      native->setDoesNotThrow();
      native->setWillReturn();
      if (name != "view" && name != "update" && !graph->HasCOWMutations())
        native->setMemoryEffects(llvm::MemoryEffects::readOnly() | llvm::MemoryEffects::inaccessibleMemOnly());
    }

    if (graph == graphs_->GetInit())
      return;
    std::vector<llvm::Type*> thunk_params{ptr_ty_};
    const auto declared = graph->GetParameterTypes().size() - graph->GetCaptures().size();
    for (size_t idx = 0; idx < declared; idx++) {
      thunk_params.push_back(i64_);
      thunk_params.push_back(i64_);
    }

    const auto fn = graph->GetFunction();
    const bool is_entry = (name == "init" || name == "update" || name == "view" || name == "main");
    const bool preserved = fn != nullptr && fn->HasDirective("preserve");
    const bool exported = fn != nullptr && !graph->IsExtern() && (is_entry || preserved);

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
    for (const auto& entry : const_boxes_) {
      if (entry.global->use_empty())
        continue;
      llvm::Value* bits =
          entry.is_string ? b_.CreatePtrToInt(StringLiteral(entry.payload_str), i64_) : b_.getInt64(entry.payload_bits);
      llvm::Value* box = CallRt("kura_rt_box", ptr_ty_, {b_.getInt64(entry.payload_tag), bits});
      // Mark header refcount as immortal (0x3FFF at bits 31..18) so Retain/Release are no-ops
      llvm::Value* hdr = b_.CreateLoad(i64_, box);
      llvm::Value* immortal_hdr = b_.CreateOr(hdr, b_.getInt64(0x3FFFULL << 18));
      b_.CreateStore(immortal_hdr, box);
      b_.CreateStore(box, entry.global);
    }
    if (graphs_->GetInit() && natives_.contains(graphs_->GetInit()))
      b_.CreateCall(natives_[graphs_->GetInit()], {});
    b_.CreateRetVoid();
  }

  void DefineRunTests(Module* kura_module) {
    auto* i32_ty = llvm::Type::getInt32Ty(ctx_);
    auto* fn_ty = llvm::FunctionType::get(i64_, {ptr_ty_, i1_}, false);
    const auto run = llvm::Function::Create(fn_ty, llvm::GlobalValue::ExternalLinkage, "kura_run_tests", module_.get());

    llvm::Value* filter_arg = run->getArg(0);     // const char* filter (nullable)
    llvm::Value* fail_fast_arg = run->getArg(1);  // bool global fail_fast

    b_.SetInsertPoint(llvm::BasicBlock::Create(ctx_, "entry", run));

    const auto& tests = kura_module->tests;
    CallRt("kura_rt_test_suite_begin", void_ty_, {b_.getInt64(tests.size()), filter_arg, fail_fast_arg});

    for (size_t idx = 0; idx < tests.size(); idx++) {
      const auto& test = tests[idx];
      llvm::Value* name_ptr = NameConstant(test.name);

      // Check filter (returns false if filter != nullptr && strstr(name, filter) == nullptr)
      auto* run_bb = llvm::BasicBlock::Create(ctx_, "test.run." + std::to_string(idx), run);
      auto* next_bb = llvm::BasicBlock::Create(ctx_, "test.next." + std::to_string(idx), run);

      llvm::Value* matches = CallRt("kura_rt_test_matches", i1_, {name_ptr, filter_arg});
      b_.CreateCondBr(matches, run_bb, next_bb);

      b_.SetInsertPoint(run_bb);
      // what the failure has to contain, or no pointer when any failure will do
      llvm::Value* expect_ptr = test.expect.empty() ? static_cast<llvm::Value*>(llvm::ConstantPointerNull::get(ptr_ty_))
                                                    : NameConstant(test.expect);
      CallRt("kura_rt_test_begin", void_ty_, {name_ptr, llvm::ConstantInt::get(i32_ty, test.flags), expect_ptr});

      if ((test.flags & TestFlag::kSkip) == 0) {
        const auto graph = graphs_->GetGraphFor(test.fn);
        if (graph && natives_.contains(graph)) {
          CallRt("kura_rt_test_invoke", void_ty_, {natives_[graph]});
        }
      }

      CallRt("kura_rt_test_end", void_ty_, {});
      b_.CreateBr(next_bb);

      b_.SetInsertPoint(next_bb);
    }

    llvm::Value* failed = CallRt("kura_rt_test_suite_end", i64_, {});
    b_.CreateRet(failed);
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
    f32_(llvm::Type::getFloatTy(ctx)),
    void_ty_(llvm::Type::getVoidTy(ctx)) {
    ptr_ty_ = llvm::PointerType::get(ctx, 0);
    value_ty_ = llvm::StructType::get(ctx, {i64_, i64_});
  }

  auto Generate(Module* m, std::string* error) -> std::unique_ptr<llvm::Module> {
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
    DefineRunTests(m);

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
  auto result = generator.Generate(module, error);
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
