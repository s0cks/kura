#ifndef KURA_C_COMPILE_H
#define KURA_C_COMPILE_H

#include <memory>
#include <string>

namespace llvm {
class LLVMContext;
class Module;
}  // namespace llvm

namespace kura {
// Compiles C to an LLVM module in `context` with the system's clang, using the
// program's `-I` and `-D` flags. This is what lets a module call functions that
// take structs by value (clang does the calling convention), call `static
// inline` functions from a header, and have the C sources named on the command
// line compiled into the program, where LLVM can inline across the language
// boundary.
//
// The compiler is `$KURA_CC`, or the clang kurac was built against, or `clang`
// on the PATH.
auto CompileCSource(llvm::LLVMContext& context, const std::string& source, std::string* error)
    -> std::unique_ptr<llvm::Module>;
auto CompileCFile(llvm::LLVMContext& context, const std::string& path, std::string* error)
    -> std::unique_ptr<llvm::Module>;
}  // namespace kura

#endif  // KURA_C_COMPILE_H
