#include "backend/c_compile.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/SourceMgr.h>

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "c_import.h"

namespace kura {
namespace {
#ifndef KURA_CLANG_EXECUTABLE
#define KURA_CLANG_EXECUTABLE ""
#endif

auto Compiler() -> std::string {
  if (const auto env = std::getenv("KURA_CC"); env && *env)
    return env;
  if (const std::string baked = KURA_CLANG_EXECUTABLE; !baked.empty() && llvm::sys::fs::exists(baked))
    return baked;
  if (const auto found = llvm::sys::findProgramByName("clang"))
    return *found;
  return {};
}

auto Slurp(const std::string& path) -> std::string {
  std::ifstream in(path);
  std::stringstream ss{};
  ss << in.rdbuf();
  return ss.str();
}

struct Temporary {
  std::string path{};

  explicit Temporary(const std::string& suffix) {
    llvm::SmallString<128> result;
    if (!llvm::sys::fs::createTemporaryFile("kura", suffix, result))
      path = std::string(result);
  }
  ~Temporary() {
    if (!path.empty())
      llvm::sys::fs::remove(path);
  }
};
}  // namespace

auto CompileCFile(llvm::LLVMContext& context, const std::string& path, std::string* error)
    -> std::unique_ptr<llvm::Module> {
  const auto compiler = Compiler();
  if (compiler.empty()) {
    if (error)
      *error = "no C compiler: kurac compiles C with clang; install it or set KURA_CC";
    return nullptr;
  }
  Temporary bitcode("bc");
  Temporary log("log");
  if (bitcode.path.empty() || log.path.empty()) {
    if (error)
      *error = "can't create a temporary file";
    return nullptr;
  }
  const auto& config = CImportConfig::Get();
  std::vector<std::string> args{compiler, "-x", "c", "-std=gnu17", "-O2", "-fPIC", "-w", "-c", "-emit-llvm"};
  for (const auto& dir : config.include_dirs)
    args.push_back("-I" + dir);
  for (const auto& define : config.defines)
    args.push_back("-D" + define);
  args.insert(args.end(), {"-o", bitcode.path, path});
  std::vector<llvm::StringRef> refs(args.begin(), args.end());
  const std::optional<llvm::StringRef> redirects[] = {std::nullopt, llvm::StringRef(log.path),
                                                      llvm::StringRef(log.path)};
  const auto status = llvm::sys::ExecuteAndWait(compiler, refs, std::nullopt, redirects);
  if (status != 0) {
    if (error)
      *error = "compiling " + path + " failed:\n" + Slurp(log.path);
    return nullptr;
  }
  llvm::SMDiagnostic diagnostic;
  auto module = llvm::parseIRFile(bitcode.path, diagnostic, context);
  if (!module) {
    if (error)
      *error = "can't read the compiled " + path + ": " + diagnostic.getMessage().str();
    return nullptr;
  }
  return module;
}

auto CompileCSource(llvm::LLVMContext& context, const std::string& source, std::string* error)
    -> std::unique_ptr<llvm::Module> {
  Temporary file("c");
  if (file.path.empty()) {
    if (error)
      *error = "can't create a temporary file";
    return nullptr;
  }
  {
    std::ofstream out(file.path);
    out << source;
  }
  return CompileCFile(context, file.path, error);
}
}  // namespace kura
