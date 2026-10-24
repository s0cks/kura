#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/TargetSelect.h>
#include <print>
#include <stdlib.h>
#include <yoga/Yoga.h>

#include "backend/flow_graph.h"
#include "backend/flow_graph_builder.h"
#include "backend/ir.h"
#include "backend/compile_type.h"
#include "backend/ir_printer.h"
#include "backend/jit.h"
#include "backend/llvm_codegen.h"
#include "backend/runtime_abi.h"
#include "backend/type_analysis.h"
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
    if (!TypeAnalysis::Run(rhs, graph, &errors)) {
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
  bool optimize = true;
  bool run = false;
  std::string function{};
  std::vector<std::string> args{};
};

auto ParseOptions(int argc, char** argv, Options* options) -> bool {
  options->filename = argv[1];
  for (auto idx = 2; idx < argc; idx++) {
    const std::string arg(argv[idx]);
    if (arg == "--emit-llvm") {
      options->emit_llvm = true;
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
  } catch (...) {
  }
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
  } catch (...) {
  }
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
               ", which isn't a " + type->ToString() + " (the command line can only pass numbers, units, bools, none and strings)";
      return false;
    }
  }
  return true;
}
}  // namespace

auto main(int argc, char** argv) -> int {
  if (argc < 2) {
    std::cerr << "usage: kurac <file> [--emit-llvm] [--no-opt] [--run <function> [args...]]" << std::endl;
    return EXIT_FAILURE;
  }

  Options options{};
  if (!ParseOptions(argc, argv, &options))
    return EXIT_FAILURE;
  const auto compile = options.emit_llvm || options.run;
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
  if (!graphs || !TypeAnalysis::Run(m, graphs, &errors)) {
    for (const auto& error : errors)
      std::cerr << "error: " << error << std::endl;
    return EXIT_FAILURE;
  }

  auto context = std::make_unique<llvm::LLVMContext>();
  std::string error;
  auto module = GenerateLLVM(*context, m, graphs, &error);
  if (!module) {
    std::cerr << "error: " << error << std::endl;
    return EXIT_FAILURE;
  }
  if (options.optimize)
    OptimizeLLVM(*module);

  if (options.emit_llvm) {
    module->print(llvm::outs(), nullptr);
    return EXIT_SUCCESS;
  }

  auto jit = Jit::Create(&error);
  if (!jit || !jit->Add(std::move(module), std::move(context), &error) || !jit->Initialize(&error)) {
    std::cerr << "error: " << error << std::endl;
    return EXIT_FAILURE;
  }

  std::vector<rt::KuraValue> args{};
  for (const auto& arg : options.args)
    args.push_back(ParseArgument(arg));
  if (!CheckArguments(m, graphs, options.function, args, &error)) {
    std::cerr << "error: " << error << std::endl;
    return EXIT_FAILURE;
  }
  rt::KuraValue result{};
  if (!jit->Call(options.function, args, &result, &error)) {
    std::cerr << "error: " << error << std::endl;
    return EXIT_FAILURE;
  }
  std::cout << "=> " << rt::Format(result) << std::endl;
  return EXIT_SUCCESS;
}
