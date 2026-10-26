#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/TargetSelect.h>
#include <print>
#include <stdlib.h>
#include <yoga/Yoga.h>

#include "backend/c_compile.h"
#include "backend/c_header.h"
#include "backend/compile_type.h"
#include "backend/flow_graph.h"
#include "backend/flow_graph_builder.h"
#include "backend/flow_graph_compiler.h"
#include "backend/ir.h"
#include "backend/ir_printer.h"
#include "backend/jit.h"
#include "backend/llvm_codegen.h"
#include "backend/purity_analysis.h"
#include "backend/runtime_abi.h"
#include "backend/type_analysis.h"
#include "c_import.h"
#include "common.h"
#include "element_compiler.h"
#include "frontend/expr_printer.h"
#include "frontend/parser.h"
#include "kura.h"
#include "module.h"

using namespace kura;

class ModulePrinter {
 private:
  std::ostream& stream_;
  Indent indent_{};

  inline auto stream() const -> std::ostream& {
    return stream_;
  }

  inline auto indent() const -> const Indent& {
    return indent_;
  }

  inline auto out() -> std::ostream& {
    return stream() << indent();
  }

 public:
  explicit ModulePrinter(std::ostream& stream, const Indent indent = 0) :
    stream_(stream),
    indent_(std::move(indent)) {}
  ~ModulePrinter() = default;

  auto PrintModule(Module* rhs) -> bool {
    if (!rhs)
      return true;

    out() << "Module: " << rhs->ToString() << std::endl;
    std::vector<std::string> errors{};
    const auto graph = FlowGraphBuilder::BuildModuleGraph(rhs, &errors);
    if (!graph) {
      for (const auto& error : errors)
        std::cerr << "error: " << error << std::endl;

      return false;
    }

    if (!TypeAnalysis::Run(rhs, graph, &errors) || !PurityAnalysis::Run(rhs, graph, &errors)) {
      for (const auto& error : errors)
        std::cerr << "error: " << error << std::endl;

      return false;
    }

    IRPrinter::Print(stream(), graph, indent());
    return true;
  }

  auto operator()(Module* rhs) -> bool {
    return PrintModule(rhs);
  }
};

namespace {
struct Options {
  std::string filename{};
  bool emit_llvm = false;
  std::string emit_object{};
  std::string emit_header{};
  bool optimize = true;
  bool run = false;
  std::string function{};
  std::vector<std::string> args{};
  std::vector<std::string> library_dirs{};
  std::vector<std::string> libraries{};
  std::vector<std::string> c_sources{};
};

// `-lm` is libm.so in one of the `-L` directories, then in the usual places.
auto FindLibrary(const Options& options, const std::string& name) -> std::string {
  std::vector<std::string> dirs = options.library_dirs;
  for (const auto dir : {"/usr/local/lib", "/usr/lib", "/usr/lib/x86_64-linux-gnu", "/lib", "/lib/x86_64-linux-gnu"})
    dirs.push_back(dir);
  for (const auto& dir : dirs) {
    for (const auto ext : {".so", ".dylib"}) {
      const auto path = dir + "/lib" + name + ext;
      if (std::filesystem::exists(path))
        return path;
    }
  }
  return {};
}

auto ParseOptions(int argc, char** argv, Options* options) -> bool {
  options->filename = argv[1];
  for (auto idx = 2; idx < argc; idx++) {
    const std::string arg(argv[idx]);
    if (arg == "--emit-llvm") {
      options->emit_llvm = true;
    } else if (arg == "--emit-obj" || arg == "--emit-header") {
      if (idx + 1 >= argc) {
        std::cerr << arg << " needs a file name" << std::endl;
        return false;
      }
      (arg == "--emit-obj" ? options->emit_object : options->emit_header) = argv[++idx];
    } else if (arg.starts_with("-I") && arg.size() > 2) {
      CImportConfig::Get().include_dirs.push_back(arg.substr(2));
    } else if (arg.starts_with("-D") && arg.size() > 2) {
      CImportConfig::Get().defines.push_back(arg.substr(2));
    } else if (arg.starts_with("-L") && arg.size() > 2) {
      options->library_dirs.push_back(arg.substr(2));
    } else if (arg.starts_with("-l") && arg.size() > 2) {
      options->libraries.push_back(arg.substr(2));
    } else if (arg.ends_with(".c")) {
      options->c_sources.push_back(arg);
    } else if (arg == "--no-opt") {
      options->optimize = false;
    } else if (arg == "--run") {
      if (idx + 1 >= argc) {
        std::cerr << "--run needs a function name" << std::endl;
        return false;
      }

      options->run = true;
      options->function = argv[++idx];
      // everything after the function is an argument for it
      while (idx + 1 < argc)
        options->args.push_back(argv[++idx]);

    } else {
      std::cerr << "unknown option: " << arg << std::endl;
      return false;
    }
  }

  return true;
}

// `12` is an Int, `1.5` a Float, `true`/`false` a Bool, `none` is None, `10px`
// a Px, and anything else is a String.
auto ParseArgument(const std::string& text) -> rt::KuraValue {
  using rt::KuraValue;
  const auto bits_of = [](const double value) {
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
  };
  if (text == "true" || text == "false")
    return {rt::kTagBool, text == "true" ? 1ULL : 0ULL};

  if (text == "none")
    return {rt::kTagNone, 0};

  try {
    size_t used = 0;
    const auto value = std::stoll(text, &used);
    if (used == text.size())
      return {rt::kTagInt, static_cast<uint64_t>(value)};

  } catch (...) {}

  try {
    size_t used = 0;
    const auto value = std::stod(text, &used);
    if (used == text.size())
      return {rt::kTagFloat, bits_of(value)};

    const auto unit = text.substr(used);
    if (unit == "px")
      return {rt::kTagPx, bits_of(value)};

    if (unit == "%")
      return {rt::kTagPct, bits_of(value)};

    if (unit == "fr")
      return {rt::kTagFr, bits_of(value)};
  } catch (...) {}

  return rt::MakeString(text);
}
// The command line can only build scalars, so the arguments of a call from it
// are checked against the function's signature: handing the code a value of the
// wrong type is undefined behaviour, not an error it can report.
auto CheckArguments(Module* module, ModuleGraph* graphs, const std::string& name,
                    const std::vector<rt::KuraValue>& args, std::string* error) -> bool {
  FlowGraph* graph = nullptr;
  for (const auto function : module->functions) {
    if (std::string(function->GetName()->GetData()) == name)
      graph = graphs->GetGraphFor(function);
  }

  if (!graph || !graph->GetReturnType()) {
    *error = "there is no function `" + name + "`";
    return false;
  }

  const auto& params = graph->GetParameterTypes();
  if (params.size() != args.size()) {
    *error = "`" + name + "` takes " + std::to_string(params.size()) + " argument(s) but " +
             std::to_string(args.size()) + " were given";
    return false;
  }

  for (size_t idx = 0; idx < args.size(); idx++) {
    const auto type = params[idx];
    const auto tag = args[idx].tag;
    bool ok = false;
    for (const auto alt : type->GetAlternatives()) {
      switch (alt->GetKind()) {
        case CompileType::kAny:
          ok = true;
          break;

        case CompileType::kNumber:
          ok |= tag == rt::kTagInt || tag == rt::kTagFloat;
          break;

        case CompileType::kInt:
          ok |= tag == rt::kTagInt;
          break;

        case CompileType::kFloat:
          ok |= tag == rt::kTagFloat;
          break;

        case CompileType::kBool:
          ok |= tag == rt::kTagBool;
          break;

        case CompileType::kNone:
          ok |= tag == rt::kTagNone;
          break;

        case CompileType::kString:
          ok |= tag == rt::kTagString;
          break;

        case CompileType::kPx:
          ok |= tag == rt::kTagPx;
          break;

        case CompileType::kPct:
          ok |= tag == rt::kTagPct;
          break;

        case CompileType::kFr:
          ok |= tag == rt::kTagFr;
          break;

        default:
          break;
      }
    }

    if (!ok) {
      *error = "argument " + std::to_string(idx + 1) + " of `" + name + "` is " + rt::Format(args[idx]) +
               ", which isn't a " + type->ToString() +
               " (the command line can only pass numbers, units, bools, none and strings)";
      return false;
    }
  }

  return true;
}
}  // namespace

auto main(int argc, char** argv) -> int {
  if (argc < 2) {
    std::cerr << "usage: kurac <file> [<c-source>.c...] [--emit-llvm] [--emit-obj <file.o>] [--emit-header <file.h>] "
                 "[--no-opt] [-I<dir>] [-D<name>] [-L<dir>] [-l<lib>] [--run <function> [args...]]"
              << std::endl;
    return EXIT_FAILURE;
  }

  Options options{};
  if (!ParseOptions(argc, argv, &options))
    return EXIT_FAILURE;
  const auto compile = options.emit_llvm || options.run || !options.emit_object.empty() || !options.emit_header.empty();
  if (!compile)
    std::println("running kurac v{}", KURA_VERSION);

  Type::Init();

#ifdef KURA_DEBUG
  if (!compile) {
    TypePrinter printer(std::cout);
    if (!Type::VisitTypes(&printer))
      return EXIT_FAILURE;

    const std::function<VisitResult(Property*)> vis = [](Property* property) {
      std::cout << " - " << property->ToString() << std::endl;
      return true;
    };
    std::cout << "Properties:" << std::endl;
    if (!Property::VisitAllProperties(vis))
      return EXIT_FAILURE;
  }
#endif  // KURA_DEBUG

  LocalScope* scope = LocalScope::New();
  Module* m = nullptr;
  Parser parser(scope);

  const auto comp_start = std::chrono::high_resolution_clock::now();
  {
    const auto result = parser.ParseModuleFromFile(options.filename, &m);
    if (!result) {
      std::cerr << "parse error: " << result;
      return EXIT_FAILURE;
    }
  }

  if (!compile) {
    ModulePrinter printer(std::cout);
    return printer(m) ? EXIT_SUCCESS : EXIT_FAILURE;
  }

  std::vector<std::string> errors{};
  const auto graphs = FlowGraphBuilder::BuildModuleGraph(m, &errors);
  if (!options.emit_header.empty()) {
    std::string guard = "KURA_";
    for (const char c : std::filesystem::path(options.emit_header).stem().string())
      guard += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::toupper(c)) : '_';
    guard += "_H";
    std::ofstream out(options.emit_header);
    if (!out) {
      std::cerr << "error: can't write " << options.emit_header << std::endl;
      return EXIT_FAILURE;
    }
    out << GenerateCHeader(m, guard);
  }

  if (!options.emit_llvm && !options.run && options.emit_object.empty())
    return EXIT_SUCCESS;
  auto context = std::make_unique<llvm::LLVMContext>();

  std::string error{};
  std::unique_ptr<llvm::LLVMContext> ctx{};
  std::unique_ptr<llvm::Module> module{};
  FlowGraphCompiler compiler(ctx, module, options.optimize);
  if (!compiler.Compile(m, graphs, errors)) {
    for (const auto& error : errors) {
      std::cerr << error << std::endl;
    }

    return EXIT_FAILURE;
  }

  // C sources named on the command line are compiled into the module, so the
  // program is one unit that LLVM can optimize across.
  for (const auto& path : options.c_sources) {
    auto compiled = CompileCFile(*context, path, &error);
    if (!compiled) {
      std::cerr << "error: " << error << std::endl;
      return EXIT_FAILURE;
    }
    if (module->getDataLayoutStr().empty())
      module->setDataLayout(compiled->getDataLayout());
    if (module->getTargetTriple().empty())
      module->setTargetTriple(compiled->getTargetTriple());
    if (llvm::Linker::linkModules(*module, std::move(compiled))) {
      std::cerr << "error: can't link " << path << " into the module" << std::endl;
      return EXIT_FAILURE;
    }
  }

  if (!options.emit_object.empty()) {
    if (!EmitObjectFile(*module, options.emit_object, &error)) {
      std::cerr << "error: " << error << std::endl;
      return EXIT_FAILURE;
    }
    if (!options.emit_llvm && !options.run)
      return EXIT_SUCCESS;
  }
  if (options.emit_llvm) {
    module->print(llvm::outs(), nullptr);
    return EXIT_SUCCESS;
  }

  auto jit = Jit::Create(&error);
  if (jit) {
    for (const auto& name : options.libraries) {
      const auto path = FindLibrary(options, name);
      if (path.empty()) {
        std::cerr << "error: can't find library -l" << name << std::endl;
        return EXIT_FAILURE;
      }
      if (!jit->LoadLibrary(path, &error)) {
        std::cerr << "error: " << error << std::endl;
        return EXIT_FAILURE;
      }
    }
  }

  if (!jit || !jit->Add(std::move(module), std::move(ctx), &error) || !jit->Initialize(&error)) {
    std::cerr << "error: " << error << std::endl;
    return EXIT_FAILURE;
  }
  const auto comp_end = std::chrono::high_resolution_clock::now();
  const auto comp_dur = std::chrono::duration_cast<std::chrono::milliseconds>(comp_end - comp_start);

  std::cout << "compilation finished in " << comp_dur.count() << "ms" << std::endl;

  std::vector<rt::KuraValue> args{};
  for (const auto& arg : options.args)
    args.push_back(ParseArgument(arg));

  if (!CheckArguments(m, graphs, options.function, args, &error)) {
    std::cerr << "error: " << error << std::endl;
    return EXIT_FAILURE;
  }

  const auto exec_start = std::chrono::high_resolution_clock::now();
  rt::KuraValue result{};
  if (!jit->Call(options.function, args, &result, &error)) {
    std::cerr << "error: " << error << std::endl;
    return EXIT_FAILURE;
  }
  const auto exec_end = std::chrono::high_resolution_clock::now();
  const auto exec_dur = std::chrono::duration_cast<std::chrono::microseconds>((exec_end - exec_start));

  std::cout << "=> " << rt::Format(result) << "  (" << exec_dur.count() << "us)" << std::endl;
  return EXIT_SUCCESS;
}
