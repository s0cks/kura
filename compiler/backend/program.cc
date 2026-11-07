#include "backend/program.h"

#include <cstring>
#include <mutex>
#include <sstream>

#include "backend/compile_type.h"
#include "backend/flow_graph_builder.h"
#include "backend/flow_graph_compiler.h"
#include "frontend/parser.h"
#include "type.h"

namespace kura {
namespace {
auto Join(const std::vector<std::string>& errors) -> std::string {
  std::string out{};
  for (const auto& error : errors) {
    if (!out.empty())
      out += "\n";
    out += error;
  }
  return out;
}
}  // namespace

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

auto Program::Load(const std::string& path, const Options& options, std::string* error)
    -> std::unique_ptr<Program> {
  static std::once_flag types_once;
  std::call_once(types_once, [] { Type::Init(); });

  LocalScope* scope = LocalScope::New();
  Module* module = nullptr;
  Parser parser(scope);
  if (const auto parsed = parser.ParseModuleFromFile(path, &module); !parsed) {
    std::ostringstream out;
    out << "parse error: " << parsed;
    *error = out.str();
    return nullptr;
  }

  std::vector<std::string> errors{};
  const auto graphs = FlowGraphBuilder::BuildModuleGraph(module, &errors, nullptr);
  if (!graphs) {
    *error = Join(errors);
    return nullptr;
  }

  std::unique_ptr<llvm::LLVMContext> context{};
  std::unique_ptr<llvm::Module> compiled{};
  FlowGraphCompiler compiler(context, compiled, options.optimize);
  if (!compiler.Compile(module, graphs, errors)) {
    *error = Join(errors);
    return nullptr;
  }

  auto jit = Jit::Create(error);
  if (!jit || !jit->Add(std::move(compiled), std::move(context), error) || !jit->Initialize(error))
    return nullptr;
  return std::unique_ptr<Program>(new Program(module, graphs, std::move(jit)));
}

auto Program::Call(const std::string& function, const std::vector<rt::KuraValue>& args, rt::KuraValue* result,
                   std::string* error) -> bool {
  return CheckArguments(module_, graphs_, function, args, error) && jit_->Call(function, args, result, error);
}

auto Program::ZeroArguments(const std::string& function, std::vector<rt::KuraValue>* args, std::string* error) const
    -> bool {
  FlowGraph* graph = nullptr;
  for (const auto fn : module_->functions) {
    if (std::string(fn->GetName()->GetData()) == function)
      graph = graphs_->GetGraphFor(fn);
  }
  if (!graph || !graph->GetReturnType()) {
    *error = "there is no function `" + function + "`";
    return false;
  }

  args->clear();
  const auto& params = graph->GetParameterTypes();
  for (size_t idx = 0; idx < params.size(); idx++) {
    const auto& alternatives = params[idx]->GetAlternatives();
    const auto kind = alternatives.empty() ? CompileType::kAny : alternatives[0]->GetKind();
    switch (kind) {
      case CompileType::kInt:
      case CompileType::kNumber:
        args->push_back({rt::kTagInt, 0});
        break;

      case CompileType::kFloat:
        args->push_back({rt::kTagFloat, 0});
        break;

      case CompileType::kBool:
        args->push_back({rt::kTagBool, 0});
        break;

      case CompileType::kString:
        args->push_back(rt::MakeString(""));
        break;

      case CompileType::kAny:
      case CompileType::kNone:
        args->push_back({rt::kTagNone, 0});
        break;

      default:
        *error = "`" + function + "` takes " + params[idx]->ToString() + " as argument " + std::to_string(idx + 1) +
                 ", which has no default; pass the arguments on the command line";
        return false;
    }
  }
  return true;
}
}  // namespace kura
