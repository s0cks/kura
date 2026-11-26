#include <argparse/argparse.hpp>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Linker/Linker.h>
#include <print>

#include "backend/c_compile.h"
#include "backend/c_header.h"
#include "backend/escape_analysis.h"
#include "backend/flow_graph.h"
#include "backend/flow_graph_builder.h"
#include "backend/flow_graph_compiler.h"
#include "backend/ir_printer.h"
#include "backend/jit.h"
#include "backend/llvm_codegen.h"
#include "backend/program.h"
#include "backend/purity_analysis.h"
#include "backend/runtime_abi.h"
#include "backend/type_analysis.h"
#include "c_import.h"
#include "cli/format.h"
#include "common.h"
#include "editorconfig_wrapper.h"
#include "explain/index.h"
#include "explain/renderer.h"
#include "explain/suggest.h"
#include "formatter/formatter.h"
#include "frontend/parser.h"
#include "frontend/syntax/parser.h"
#include "frontend/syntax/source.h"
#include "heap/heap.h"
#include "kura.h"
#include "lint/linter.h"
#include "module.h"

#ifdef KURA_HAS_HEADLESS_RENDERER
#include "renderer/headless/renderer.h"
#endif  // KURA_HAS_HEADLESS_RENDERER

using namespace kura;

namespace {
static inline auto GetLastErrorCodePath() -> std::filesystem::path {
  namespace fs = std::filesystem;

  const auto current_workdir = fs::weakly_canonical(fs::current_path());
  const auto hash = std::hash<std::string>{}(current_workdir.string());

  fs::path cache_dir;
  if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg) {
    cache_dir = fs::path(xdg) / "kura";
  } else if (const char* home = std::getenv("HOME"); home && *home) {
    cache_dir = fs::path(home) / ".cache" / "kura";
  } else {
    cache_dir = fs::temp_directory_path() / "kura";
  }

  std::error_code ec;
  fs::create_directories(cache_dir, ec);

  return cache_dir / std::format("last_{:016x}.txt", hash);
}

struct CliOptions {
  std::string error_code{};
  std::string filename{};
  std::vector<std::string> c_sources{};
  std::vector<std::string> include_dirs{};
  std::vector<std::string> defines{};
  std::vector<std::string> library_dirs{};
  std::vector<std::string> libraries{};

  bool optimize = true;
  bool dump_ir = false;
  bool dump_dom = false;
  bool dump_draw = false;

#ifdef KURA_HAS_EDITORCONFIG
  bool editorconfig = true;
#endif  // KURA_HAS_EDITORCONFIG

  // compile Tracy zones into the program (see TraceOptions)
  bool trace = false;
  bool emit_llvm = false;
  std::string emit_object{};
  std::string emit_header{};

  // Mode flags
  bool is_test = false;
  bool is_lint = false;
  bool is_bench = false;
  bool is_run = false;
  bool is_format = false;
  bool is_explain = false;
  bool is_explain_list = false;
  bool explain_last = false;
  std::string filter{};
  bool fail_fast = false;
  bool pager = true;

  std::string function{};
  std::vector<std::string> args{};

  cli::Format format = cli::Format::kPretty;
};

// What `--trace` means for the graphs: zones in the program, attributed to the file it was built from. A kurac built
// without tracing support can still compile them (they are two runtime calls that do nothing), but says so, because
// a profiler would otherwise just show nothing.
auto TraceOptionsFor(const CliOptions& options) -> TraceOptions {
  TraceOptions trace{};
  trace.enabled = options.trace;
  trace.file = options.filename;
  if (options.trace && kura_rt_tracy_enabled() == 0)
    std::cerr << "warning: --trace: this kurac was built without tracing support (KURA_ADD_TRACING_SUPPORT), so "
                 "the zones are compiled in but nothing records them"
              << std::endl;
  return trace;
}

auto FindLibrary(const CliOptions& options, const std::string& name) -> std::string {
  std::vector<std::string> dirs = options.library_dirs;
  for (const auto* dir : {"/usr/local/lib", "/usr/lib", "/usr/lib/x86_64-linux-gnu", "/lib", "/lib/x86_64-linux-gnu"})
    dirs.emplace_back(dir);
  for (const auto& dir : dirs) {
    for (const auto* ext : {".so", ".dylib"}) {
      const auto path = dir + "/lib" + name + ext;
      if (std::filesystem::exists(path))
        return path;
    }
  }
  return {};
}

static inline auto Slurp(const std::string& filename) -> std::optional<std::string> {
  std::ifstream file(filename, std::ios::in | std::ios::binary);
  if (!file.is_open())
    return std::nullopt;

  return {std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>())};
}

static inline auto ParseTree(const std::string filename) -> std::unique_ptr<kura::syntax::SyntaxTree> {
  const auto contents = Slurp(filename);
  if (!contents)
    return nullptr;

  return kura::syntax::ParseText(filename, *contents);
}

static inline void RegisterPagerArgs(argparse::ArgumentParser& cmd, CliOptions& opts) {
  auto& pager_group = cmd.add_mutually_exclusive_group();
  pager_group.add_argument("-p", "--pager").help("use pager for output (default)").flag().action([&opts](const auto&) {
    opts.pager = true;
  });
  pager_group.add_argument("--no-pager").help("disable pager for output").flag().action([&opts](const auto&) {
    opts.pager = false;
  });
}

#ifdef KURA_HAS_EDITORCONFIG

static inline void RegisterEditorconfigArgs(argparse::ArgumentParser& cmd, CliOptions& opts) {
  auto& group = cmd.add_mutually_exclusive_group();
  group.add_argument("--editorconfig").help("use .editorconfig (default)").flag().action([&opts](const auto&) {
    opts.editorconfig = true;
  });
  group.add_argument("--no-editorconfig").help("disable editorconfig support").flag().action([&opts](const auto&) {
    opts.editorconfig = false;
  });
}

#endif  // KURA_HAS_EDITORCONFIG

void RegisterCommonArgs(argparse::ArgumentParser& cmd, CliOptions& opts) {
  cmd.add_argument("file").help("Kura source file (.kui)").store_into(opts.filename);
  cmd.add_argument("--c-source")
      .help("Additional C source file to compile and link")
      .append()
      .store_into(opts.c_sources);
  cmd.add_argument("-I", "--include-dir")
      .help("Add directory to C header include search path")
      .append()
      .store_into(opts.include_dirs);
  cmd.add_argument("-D", "--define").help("Define preprocessor macro for C headers").append().store_into(opts.defines);
  cmd.add_argument("-L", "--library-dir")
      .help("Add directory to library search path")
      .append()
      .store_into(opts.library_dirs);
  cmd.add_argument("-l", "--library").help("Link against shared library").append().store_into(opts.libraries);
  cmd.add_argument("--no-opt").help("Disable LLVM O2 optimization passes").flag().action([&](const auto&) {
    opts.optimize = false;
  });
  cmd.add_argument("--dump-dom").help("Dump lowered UI DOM tree").flag().store_into(opts.dump_dom);
  cmd.add_argument("--trace")
      .help("Compile Tracy zones into the program: one per function, and one for each frame of `view`")
      .flag()
      .store_into(opts.trace);
}

auto ParseCli(int argc, char** argv, CliOptions& opts) -> bool {
  argparse::ArgumentParser program("kurac", KURA_VERSION);
  program.add_description("Compiler, runner, and test harness for the Kura UI DSL");

  // 1. `kurac run <file> <fn> [args...]`
  argparse::ArgumentParser run_cmd("run");
  run_cmd.add_description("JIT-compile and execute a Kura function");
  RegisterCommonArgs(run_cmd, opts);
  run_cmd.add_argument("function").help("Entrypoint function to invoke").store_into(opts.function);
  run_cmd.add_argument("args").help("Arguments passed to the function").remaining().store_into(opts.args);
  run_cmd.add_argument("--dump-draw").help("Print headless renderer draw commands").flag().store_into(opts.dump_draw);

  // 2. `kurac test <file> [--filter <pat>] [--fail-fast]`
  argparse::ArgumentParser test_cmd("test");
  test_cmd.add_description("Compile and run all `test` blocks in a file");
  RegisterCommonArgs(test_cmd, opts);
  test_cmd.add_argument("-f", "--filter")
      .help("Only run tests matching substring")
      .default_value(std::string{})
      .store_into(opts.filter);
  test_cmd.add_argument("--fail-fast")
      .help("Abort each test on its first failing assertion")
      .flag()
      .store_into(opts.fail_fast);

  // 3. `kurac bench <file> [--filter <pat>]`
  argparse::ArgumentParser bench_cmd("bench");
  bench_cmd.add_description("Compile and run all `bench` blocks in a file");
  RegisterCommonArgs(bench_cmd, opts);
  bench_cmd.add_argument("-f", "--filter")
      .help("Only run benchmarks matching substring")
      .default_value(std::string{})
      .store_into(opts.filter);

  // 4. `kurac build <file> [--emit-llvm] [--emit-obj <out>] [--emit-header <out>]`
  argparse::ArgumentParser build_cmd("build");
  build_cmd.add_description("Compile a Kura module to LLVM IR, object file, or C header");
  RegisterCommonArgs(build_cmd, opts);
  build_cmd.add_argument("--emit-llvm").help("Print generated LLVM IR to stdout").flag().store_into(opts.emit_llvm);
  build_cmd.add_argument("--emit-obj")
      .help("Emit native object file (.o)")
      .default_value(std::string{})
      .store_into(opts.emit_object);
  build_cmd.add_argument("--emit-header")
      .help("Emit C FFI header (.h)")
      .default_value(std::string{})
      .store_into(opts.emit_header);

  // 5. `kurac check <file> [--dump-ir]`
  argparse::ArgumentParser check_cmd("check");
  check_cmd.add_description("Parse, type-check, and analyze a Kura file without codegen");
  RegisterCommonArgs(check_cmd, opts);
  check_cmd.add_argument("--dump-ir").help("Print Kura SSA IR after analysis passes").flag().store_into(opts.dump_ir);

  argparse::ArgumentParser lint_cmd("lint");
  lint_cmd.add_description("Lint a file");
  RegisterCommonArgs(lint_cmd, opts);
#ifdef KURA_HAS_EDITORCONFIG
  RegisterEditorconfigArgs(lint_cmd, opts);
#endif  // KURA_HAS_EDITORCONFIG
  auto& color_group = lint_cmd.add_mutually_exclusive_group();
  // clang-format off
  color_group.add_argument("-f", "--format")
    .default_value("pretty")
    .choices("plain", "pretty", "json", "jsonl")
    .action([&opts](const std::string& value) {
        const auto format = cli::ParseFormat(value);
        if(format)
          opts.format = format.value();
    });
  // clang-format on

  argparse::ArgumentParser fmt_cmd("fmt");
  fmt_cmd.add_description("Format a file");
  RegisterCommonArgs(fmt_cmd, opts);
#ifdef KURA_HAS_EDITORCONFIG
  RegisterEditorconfigArgs(fmt_cmd, opts);
#endif  // KURA_HAS_EDITORCONFIG

  argparse::ArgumentParser explain_cmd("explain");
#ifdef KURA_HAS_EDITORCONFIG
  RegisterEditorconfigArgs(explain_cmd, opts);
#endif  // KURA_HAS_EDITORCONFIG
  RegisterPagerArgs(explain_cmd, opts);
  explain_cmd.add_description("Explain an error");
  explain_cmd.add_argument("error_code").store_into(opts.error_code).nargs(argparse::nargs_pattern::optional);
  explain_cmd.add_argument("-f", "--format")
      .default_value("pretty")
      .choices("plain", "pretty")
      .action([&opts](const std::string& value) {
        const auto format = cli::ParseFormat(value);
        if (format)
          opts.format = format.value();
      });
  explain_cmd.add_argument("--list").flag().default_value(false).store_into(opts.is_explain_list);
  explain_cmd.add_argument("--last").flag().default_value(false).implicit_value(true).store_into(opts.explain_last);

  program.add_subparser(run_cmd);
  program.add_subparser(test_cmd);
  program.add_subparser(bench_cmd);
  program.add_subparser(build_cmd);
  program.add_subparser(check_cmd);
  program.add_subparser(lint_cmd);
  program.add_subparser(fmt_cmd);
  program.add_subparser(explain_cmd);

  try {
    program.parse_args(argc, argv);
  } catch (const std::exception& err) {
    std::cerr << err.what() << "\n\n" << program;
    return false;
  }

  if (program.is_subcommand_used("run")) {
    opts.is_run = true;
  } else if (program.is_subcommand_used("test")) {
    opts.is_test = true;
  } else if (program.is_subcommand_used("bench")) {
    opts.is_bench = true;
  } else if (program.is_subcommand_used("fmt")) {
    opts.is_format = true;
  } else if (program.is_subcommand_used("explain")) {
    opts.is_explain = true;
  } else if (program.is_subcommand_used("build")) {
    if (!opts.emit_llvm && opts.emit_object.empty() && opts.emit_header.empty()) {
      std::cerr << "build requires at least one of --emit-llvm, --emit-obj, or --emit-header\n";
      return false;
    }
  } else if (program.is_subcommand_used("check")) {
    // check mode
  } else if (program.is_subcommand_used("lint")) {
    opts.is_lint = true;
  } else {
    std::cerr << program;
    return false;
  }

  for (const auto& dir : opts.include_dirs)
    CImportConfig::Get().include_dirs.push_back(dir);
  for (const auto& def : opts.defines)
    CImportConfig::Get().defines.push_back(def);

  return true;
}

auto RunCheck(Module* m, const CliOptions& opts) -> bool {
  std::vector<std::string> errors{};
  const auto graph =
      FlowGraphBuilder::BuildModuleGraph(m, &errors, opts.dump_dom ? &std::cout : nullptr, TraceOptionsFor(opts));
  if (!graph || !TypeAnalysis::Run(m, graph, &errors) || !PurityAnalysis::Run(m, graph, &errors)) {
    for (const auto& err : errors)
      std::cerr << "error: " << err << '\n';
    return false;
  }
  for (FlowGraph* fg : graph->GetGraphs())
    fg->OptimizeCOWUpdates();
  EscapeAnalysis::Run(graph);
  for (FlowGraph* fg : graph->GetGraphs()) {
    if (fg != graph->GetInit())
      fg->InsertARC();
  }
  if (opts.dump_ir)
    IRPrinter::Print(std::cout, graph, 0);
  return true;
}
}  // namespace

auto main(int argc, char** argv) -> int {
  CliOptions opts{};
  if (!ParseCli(argc, argv, opts))
    return EXIT_FAILURE;

  const auto heap = new heap::Heap();
  KASSERT(heap);
  heap::Heap::SetCurrent(heap);
  Type::Init();

  if (opts.is_explain) {
    const auto color_mode = (opts.format == cli::Format::kPlain) ? rang::control::Off : rang::control::Auto;
    rang::setControlMode(color_mode);

    explain::RenderOptions render_opts{
        .color = color_mode,
        .tab_width = 2,
        .base_indent = 1,
    };

#ifdef KURA_HAS_EDITORCONFIG
    if (opts.editorconfig) {
      try {
        auto props = parse_editorconfig("explain.kui");
        if (auto it = props.find("indent_size"); it != props.end()) {
          long long parsed_indent = 0;
          const auto& val = it->second;
          auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), parsed_indent);
          if (ec == std::errc{} && ptr == val.data() + val.size() && parsed_indent >= 0)
            render_opts.tab_width = static_cast<decltype(render_opts.tab_width)>(parsed_indent);
        }
      } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
      }
    }
#endif  // KURA_HAS_EDITORCONFIG

    if (opts.is_explain_list) {
      explain::list_error_codes(render_opts);
      return EXIT_SUCCESS;
    }

    if (opts.explain_last || opts.error_code.empty()) {
      const auto filename = GetLastErrorCodePath();
      std::ifstream file(filename);
      if (!file.is_open()) {
        std::cerr << rang::style::bold << rang::fg::cyan << "note: " << rang::style::reset << rang::fg::reset
                  << "no diagnostics recorded from the last run (everything compiled cleanly)\n";
        return EXIT_SUCCESS;
      }

      explain::RenderOptions buf_opts = render_opts;
      if (buf_opts.color == rang::control::Auto) {
        buf_opts.color = explain::stdout_is_tty() ? rang::control::Force : rang::control::Off;
      }

      std::ostringstream combined_out{};
      explain::TerminalRenderer renderer(combined_out, buf_opts);

      std::vector<std::string> missing{};
      std::string json_line{};
      size_t rendered_count = 0;

      while (std::getline(file, json_line)) {
        if (!json_line.empty() && json_line.back() == '\r')
          json_line.pop_back();
        if (json_line.empty())
          continue;

        json_error_t jerr{};
        json_t* root = json_loads(json_line.c_str(), 0, &jerr);
        if (!root) {
          std::cerr << rang::style::bold << rang::fg::red << "error" << rang::style::reset << rang::fg::reset
                    << ": corrupted diagnostic entry in `" << filename.string() << "`\n";
          continue;
        }

        const char* raw_code = json_string_value(json_object_get(root, "code"));
        std::string code = raw_code ? raw_code : "";
        std::ranges::transform(code, code.begin(), [](unsigned char c) {
          return static_cast<char>(std::toupper(c));
        });

        const auto explanation = explain::load_explanation(code);
        if (!explanation) {
          if (!code.empty())
            missing.push_back(std::move(code));
          json_decref(root);
          continue;
        }

        if (rendered_count == 0) {
          combined_out << '\n';
        } else {
          renderer.render_separator();
        }

        renderer.render_fragment(explanation->text, root);
        rendered_count++;
        json_decref(root);
      }

      // File existed but had 0 valid records (clean build truncated the file)
      if (rendered_count == 0 && missing.empty()) {
        std::cerr << rang::style::bold << rang::fg::cyan << "note: " << rang::style::reset << rang::fg::reset
                  << "no diagnostics recorded from the last run (everything compiled cleanly)\n";
        return EXIT_SUCCESS;
      }

      if (rendered_count > 0) {
        if (opts.pager) {
          explain::render_with_pager(combined_out.str());
        } else {
          std::cout << combined_out.str();
          std::cout.flush();
        }
      }

      for (const auto& m : missing) {
        std::cerr << rang::style::bold << rang::fg::red << "error" << rang::style::reset << rang::fg::reset
                  << ": unknown error code: `" << m << "`\n";

        auto suggestions = explain::suggest_error_codes(m);
        explain::print_suggestions(std::cerr, suggestions.span());
      }

      return missing.empty() ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    // Explicit error code path: `kurac explain K0034`
    std::ranges::transform(opts.error_code, opts.error_code.begin(), [](unsigned char c) {
      return static_cast<char>(std::toupper(c));
    });

    const auto explanation = explain::load_explanation(opts.error_code);
    if (!explanation) {
      std::cerr << rang::style::bold << rang::fg::red << "error" << rang::style::reset << rang::fg::reset
                << ": unknown error code: `" << opts.error_code << "`\n";

      auto suggestions = explain::suggest_error_codes(opts.error_code);
      explain::print_suggestions(std::cerr, suggestions.span());
      return EXIT_FAILURE;
    }

    if (opts.pager) {
      explain::RenderOptions buf_opts = render_opts;
      if (buf_opts.color == rang::control::Auto) {
        buf_opts.color = explain::stdout_is_tty() ? rang::control::Force : rang::control::Off;
      }

      std::ostringstream buf{};
      explain::TerminalRenderer renderer(buf, buf_opts);
      renderer.render(explanation->text);

      explain::render_with_pager(buf.str(), buf_opts);
    } else {
      explain::TerminalRenderer renderer(std::cout, render_opts);
      renderer.render(explanation->text);
    }

    return EXIT_SUCCESS;
  }

  if (opts.is_lint) {
    const auto contents = Slurp(opts.filename);
    const auto tree = kura::syntax::ParseText(opts.filename, *contents);
    if (!tree) {
      std::cerr << "failed to parse syntax tree" << std::endl;
      return EXIT_FAILURE;
    }

    lint::Linter linter(opts.format);
    linter.Lint(*tree);
    return EXIT_SUCCESS;
  }

  if (opts.is_format) {
    fmt::Config config{};

#ifdef KURA_HAS_EDITORCONFIG
    if (opts.editorconfig) {
      try {
        auto props = parse_editorconfig("explain.kui");
        if (auto it = props.find("indent_size"); it != props.end()) {
          long long parsed_indent = 0;
          const auto& val = it->second;
          auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), parsed_indent);
          if (ec == std::errc{} && ptr == val.data() + val.size() && parsed_indent >= 0)
            config.indent_size = static_cast<decltype(config.indent_size)>(parsed_indent);
        }
      } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
      }
    }
#endif  // KURA_HAS_EDITORCONFIG

    fmt::ParseConfig(config);

    const auto contents = Slurp(opts.filename);
    const auto tree = kura::syntax::ParseText(opts.filename, *contents);
    if (!tree) {
      std::cerr << "failed to parse syntax tree" << std::endl;
      return EXIT_FAILURE;
    }

    fmt::Formatter::Format(std::cout, (*tree), std::move(config));
    return EXIT_SUCCESS;
  }

  LocalScope* scope = LocalScope::New();
  Module* m = nullptr;
  Parser parser(scope);

  const auto comp_start = std::chrono::high_resolution_clock::now();
  const auto parse_res = parser.ParseModuleFromFile(opts.filename, &m, opts.is_test, opts.is_bench);
  if (!parse_res) {
    std::cerr << "parse error: " << parse_res;
    return EXIT_FAILURE;
  }

  // `kurac check` stops after IR analysis
  if (!opts.is_run && !opts.is_test && !opts.is_bench && !opts.emit_llvm && !opts.is_lint && opts.emit_object.empty() &&
      opts.emit_header.empty()) {
    return RunCheck(m, opts) ? EXIT_SUCCESS : EXIT_FAILURE;
  }

  std::vector<std::string> errors{};
  const auto graphs =
      FlowGraphBuilder::BuildModuleGraph(m, &errors, opts.dump_dom ? &std::cerr : nullptr, TraceOptionsFor(opts));
  if (!graphs) {
    for (const auto& err : errors)
      std::cerr << "error: " << err << '\n';
    return EXIT_FAILURE;
  }

  if (!opts.emit_header.empty()) {
    std::string guard = "KURA_";
    for (const char c : std::filesystem::path(opts.emit_header).stem().string())
      guard += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::toupper(c)) : '_';
    guard += "_H";
    std::ofstream out(opts.emit_header);
    if (!out) {
      std::cerr << "error: can't write " << opts.emit_header << '\n';
      return EXIT_FAILURE;
    }
    out << GenerateCHeader(m, guard);
  }

  if (!opts.is_run && !opts.is_test && !opts.is_bench && !opts.emit_llvm && opts.emit_object.empty())
    return EXIT_SUCCESS;

  std::string error{};
  std::unique_ptr<llvm::LLVMContext> ctx{};
  std::unique_ptr<llvm::Module> module{};
  FlowGraphCompiler compiler(ctx, module, opts.optimize);
  if (!compiler.Compile(m, graphs, errors)) {
    for (const auto& err : errors)
      std::cerr << err << '\n';
    return EXIT_FAILURE;
  }

  for (const auto& path : opts.c_sources) {
    auto compiled = CompileCFile(*ctx, path, &error);
    if (!compiled) {
      std::cerr << "error: " << error << '\n';
      return EXIT_FAILURE;
    }
    if (module->getDataLayoutStr().empty())
      module->setDataLayout(compiled->getDataLayout());
    if (module->getTargetTriple().empty())
      module->setTargetTriple(compiled->getTargetTriple());
    if (llvm::Linker::linkModules(*module, std::move(compiled))) {
      std::cerr << "error: can't link " << path << " into the module\n";
      return EXIT_FAILURE;
    }
  }
  if (!opts.emit_object.empty()) {
    if (!EmitObjectFile(*module, opts.emit_object, &error)) {
      std::cerr << "error: " << error << '\n';
      return EXIT_FAILURE;
    }
    if (!opts.emit_llvm && !opts.is_run && !opts.is_test && !opts.is_bench)
      return EXIT_SUCCESS;
  }
  if (opts.emit_llvm) {
    module->print(llvm::outs(), nullptr);
    return EXIT_SUCCESS;
  }
  auto jit = Jit::Create(&error);
  if (jit) {
    for (const auto& name : opts.libraries) {
      const auto path = FindLibrary(opts, name);
      if (path.empty()) {
        std::cerr << "error: can't find library -l" << name << '\n';
        return EXIT_FAILURE;
      }
      if (!jit->LoadLibrary(path, &error)) {
        std::cerr << "error: " << error << '\n';
        return EXIT_FAILURE;
      }
    }
  }
  if (!jit || !jit->Add(std::move(module), std::move(ctx), &error) || !jit->Initialize(&error)) {
    std::cerr << "error: " << error << '\n';
    return EXIT_FAILURE;
  }
  const auto comp_end = std::chrono::high_resolution_clock::now();
  const auto comp_dur = std::chrono::duration_cast<std::chrono::milliseconds>(comp_end - comp_start);
  std::cout << "compilation finished in " << comp_dur.count() << "ms\n";
#ifdef KURA_HAS_HEADLESS_RENDERER
  renderer::HeadlessRenderer headless(renderer::HeadlessOptions{.rasterize = false});
  auto headless_abi = headless.Abi();
  kura_rt_set_renderer(&headless_abi);
#endif  // KURA_HAS_HEADLESS_RENDERER

  // ── kurac test ────────────────────────────────────────────────────────────
  if (opts.is_test) {
    const auto tree = ParseTree(opts.filename);  // TODO(@s0cks): double parse
    kura_rt_set_source(&tree->GetSource());

    uint64_t failed = 0;
    if (!jit->RunTests(opts.filter.empty() ? nullptr : opts.filter.c_str(), opts.fail_fast, &failed, &error)) {
      std::cerr << "error: " << error << '\n';
      return EXIT_FAILURE;
    }
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
  }

  // ── kurac bench ───────────────────────────────────────────────────────────
  if (opts.is_bench) {
    uint64_t over_budget = 0;
    if (!jit->RunBenchmarks(opts.filter.empty() ? nullptr : opts.filter.c_str(), &over_budget, &error)) {
      std::cerr << "error: " << error << '\n';
      return EXIT_FAILURE;
    }
    return over_budget == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
  }

  // ── kurac run ─────────────────────────────────────────────────────────────
  std::vector<rt::KuraValue> args{};
  for (const auto& arg : opts.args)
    args.push_back(ParseArgument(arg));
  if (!CheckArguments(m, graphs, opts.function, args, &error)) {
    std::cerr << "error: " << error << '\n';
    return EXIT_FAILURE;
  }
  const auto exec_start = std::chrono::high_resolution_clock::now();
  rt::KuraValue result{};
  if (!jit->Call(opts.function, args, &result, &error)) {
    std::cerr << "error: " << error << '\n';
    return EXIT_FAILURE;
  }
  const auto exec_end = std::chrono::high_resolution_clock::now();
  const auto exec_dur = std::chrono::duration_cast<std::chrono::microseconds>(exec_end - exec_start);
#ifdef KURA_HAS_HEADLESS_RENDERER
  if (opts.dump_draw) {
    for (const auto& command : headless.GetCommands()) {
      if (command.kind == renderer::Command::Kind::kText) {
        const auto byte = [](const float channel) {
          return static_cast<uint8_t>(std::lround(channel * 255.0f));
        };
        char color[16];
        std::snprintf(color, sizeof(color), "#%02x%02x%02x%02x", byte(command.color.r), byte(command.color.g),
                      byte(command.color.b), byte(command.color.a));
        std::cout << "text x=" << command.from.x << " y=" << command.from.y << " size=" << command.font_size
                  << " family="
                     " << command.family << "
                     " color="
                  << color
                  << " value="
                     " << command.text << "
                     "\n";
        continue;
      }
      if (command.kind == renderer::Command::Kind::kFillRect) {
        std::cout << "fill-rect x=" << command.rect.x << " y=" << command.rect.y << " w=" << command.rect.width
                  << " h=" << command.rect.height << '\n';
      }
    }
  }
  if (headless.GetFrameCount() > 0) {
    std::cout << "rendered " << headless.GetFrameCount() << " frame(s), " << headless.GetCommands().size()
              << " draw command(s) in the last one\n";
  }
#endif  // KURA_HAS_HEADLESS_RENDERER
  if (result.tag != rt::kTagInvalid)
    std::cout << "=> " << rt::Format(result) << "  (" << exec_dur.count() << "us)\n";
  else
    std::cout << "finished in " << exec_dur.count() << "us\n";
  return EXIT_SUCCESS;
}
