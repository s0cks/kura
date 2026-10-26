#include "backend/jit.h"

#include <llvm/ExecutionEngine/Orc/Core.h>
#include <llvm/ExecutionEngine/Orc/ExecutionUtils.h>
#include <llvm/ExecutionEngine/Orc/ExecutorProcessControl.h>
#include <llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h>
#include <llvm/ExecutionEngine/Orc/Mangling.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/TargetSelect.h>

#include "backend/llvm_codegen.h"

namespace kura {
auto Jit::Create(std::string* error) -> std::unique_ptr<Jit> {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  auto created = llvm::orc::LLJITBuilder().create();
  if (!created) {
    if (error)
      *error = "failed to create the JIT: " + llvm::toString(created.takeError());
    return nullptr;
  }
  auto jit = std::move(*created);

  llvm::orc::SymbolMap symbols{};
#define BIND_RUNTIME_FUNCTION(Name) \
  symbols[jit->mangleAndIntern(#Name)] = {llvm::orc::ExecutorAddr::fromPtr(&Name), llvm::JITSymbolFlags::Exported};
  KURA_RUNTIME_FUNCTIONS(BIND_RUNTIME_FUNCTION)
#undef BIND_RUNTIME_FUNCTION
  if (auto err = jit->getMainJITDylib().define(llvm::orc::absoluteSymbols(std::move(symbols)))) {
    if (error)
      *error = "failed to bind the runtime: " + llvm::toString(std::move(err));
    return nullptr;
  }
  // Whatever the process itself links (libc, libm) is callable too.
  const auto prefix = jit->getDataLayout().getGlobalPrefix();
  auto process = llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(prefix);
  if (!process) {
    if (error)
      *error = "failed to expose the process symbols: " + llvm::toString(process.takeError());
    return nullptr;
  }
  jit->getMainJITDylib().addGenerator(std::move(*process));
  return std::unique_ptr<Jit>(new Jit(std::move(jit)));
}

auto Jit::LoadLibrary(const std::string& path, std::string* error) -> bool {
  const auto prefix = jit_->getDataLayout().getGlobalPrefix();
  auto library = llvm::orc::DynamicLibrarySearchGenerator::Load(path.c_str(), prefix);
  if (!library) {
    if (error)
      *error = "can't load library " + path + ": " + llvm::toString(library.takeError());
    return false;
  }
  jit_->getMainJITDylib().addGenerator(std::move(*library));
  return true;
}

auto Jit::Add(std::unique_ptr<llvm::Module> module, std::unique_ptr<llvm::LLVMContext> context, std::string* error)
    -> bool {
  if (auto err = jit_->addIRModule(llvm::orc::ThreadSafeModule(std::move(module), std::move(context)))) {
    if (error)
      *error = "failed to add the module to the JIT: " + llvm::toString(std::move(err));
    return false;
  }
  return true;
}

auto Jit::Initialize(std::string* error) -> bool {
  auto symbol = jit_->lookup("kura_module_init");
  if (!symbol) {
    if (error)
      *error = "kura_module_init: " + llvm::toString(symbol.takeError());
    return false;
  }
  symbol->toPtr<void (*)()>()();
  return true;
}

auto Jit::Call(const std::string& function, const std::vector<rt::KuraValue>& args, rt::KuraValue* result,
               std::string* error) -> bool {
  auto symbol = jit_->lookup(EntryPointName(function));
  if (!symbol) {
    llvm::consumeError(symbol.takeError());
    if (error)
      *error = "there is no function `" + function + "`";
    return false;
  }

  using rt::KuraValue;
  const auto fn = symbol->toPtr<void*>();
  const auto& a = args;
  switch (args.size()) {
    case 0:
      *result = reinterpret_cast<KuraValue (*)(void*)>(fn)(nullptr);
      // TODO(@s0cks): remove this is a hack for void returns of view func
      if (function == "view")
        (*result) = KuraValue{};
      return true;
    case 1:
      *result = reinterpret_cast<KuraValue (*)(void*, uint64_t, uint64_t)>(fn)(nullptr, a[0].tag, a[0].bits);
      // TODO(@s0cks): remove this is a hack for void returns of view func
      if (function == "view")
        (*result) = KuraValue{};
      return true;
    case 2:
      *result = reinterpret_cast<KuraValue (*)(void*, uint64_t, uint64_t, uint64_t, uint64_t)>(fn)(
          nullptr, a[0].tag, a[0].bits, a[1].tag, a[1].bits);
      // TODO(@s0cks): remove this is a hack for void returns of view func
      if (function == "view")
        (*result) = KuraValue{};
      return true;
    case 3:
      *result = reinterpret_cast<KuraValue (*)(void*, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t)>(fn)(
          nullptr, a[0].tag, a[0].bits, a[1].tag, a[1].bits, a[2].tag, a[2].bits);
      // TODO(@s0cks): remove this is a hack for void returns of view func
      if (function == "view")
        (*result) = KuraValue{};
      return true;
    case 4:
      *result = reinterpret_cast<KuraValue (*)(void*, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                                               uint64_t, uint64_t)>(fn)(
          nullptr, a[0].tag, a[0].bits, a[1].tag, a[1].bits, a[2].tag, a[2].bits, a[3].tag, a[3].bits);
      // TODO(@s0cks): remove this is a hack for void returns of view func
      if (function == "view")
        (*result) = KuraValue{};
      return true;
    default:
      if (error)
        *error = "calling a function with more than 4 arguments from the command line isn't supported";
      return false;
  }
}
}  // namespace kura
