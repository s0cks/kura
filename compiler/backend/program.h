#ifndef KURA_PROGRAM_H
#define KURA_PROGRAM_H

#include <memory>
#include <string>
#include <vector>

#include "backend/flow_graph.h"
#include "backend/jit.h"
#include "backend/runtime_abi.h"
#include "module.h"

namespace kura {
// A `.kui` file that has been parsed, compiled and loaded into a JIT: what a host that runs Kura
// programs (rather than compiling them ahead of time) holds on to. Calls into it go through the
// runtime ABI, so the host sets the thread's renderer (kura_rt_set_renderer) before calling a view.
class Program {
 public:
  struct Options {
    bool optimize = true;
  };

 private:
  Module* module_;
  ModuleGraph* graphs_;
  std::unique_ptr<Jit> jit_;

  Program(Module* module, ModuleGraph* graphs, std::unique_ptr<Jit> jit) :
    module_(module),
    graphs_(graphs),
    jit_(std::move(jit)) {}

 public:
  ~Program() = default;
  Program(const Program&) = delete;
  Program& operator=(const Program&) = delete;

  // The arguments are checked against the function's signature first: handing compiled code a
  // value of the wrong type is undefined behaviour, not an error it can report.
  auto Call(const std::string& function, const std::vector<rt::KuraValue>& args, rt::KuraValue* result,
            std::string* error) -> bool;

  // Whether the program defines `function`: a host asks for `init` and `update`, which are optional.
  auto HasFunction(const std::string& function) const -> bool;

  // Whether `function`'s parameter number `param` (from 0) takes a value like `value`: the same check a call
  // makes. A host asks it of `update` before sending a built-in message the program may not have asked for.
  auto Accepts(const std::string& function, size_t param, const rt::KuraValue& value) const -> bool;

  // The number of parameters of `function`, or -1 if there is no such function.
  auto GetArity(const std::string& function) const -> int;

  // One value per parameter of `function`, each the zero of its type (0, 0.0, false, none, ""),
  // for a host that has nothing better to pass. False, with `error` set, if a parameter has no
  // such value.
  auto ZeroArguments(const std::string& function, std::vector<rt::KuraValue>* args, std::string* error) const -> bool;

  // Parses, compiles and loads `path`. Null, with `error` saying why, if any step fails.
  static auto Load(const std::string& path, const Options& options, std::string* error) -> std::unique_ptr<Program>;
};

// `12` is an Int, `1.5` a Float, `true`/`false` a Bool, `none` is None, `10px` a Px, and anything
// else is a String: the values a command line can build.
auto ParseArgument(const std::string& text) -> rt::KuraValue;

// See Program::Call. Exposed for hosts, like `kura --run`, that load the module themselves.
auto CheckArguments(Module* module, ModuleGraph* graphs, const std::string& name,
                    const std::vector<rt::KuraValue>& args, std::string* error) -> bool;
}  // namespace kura

#endif  // KURA_PROGRAM_H
