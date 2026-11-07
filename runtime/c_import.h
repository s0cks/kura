#ifndef KURA_C_IMPORT_H
#define KURA_C_IMPORT_H

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "c_type.h"

namespace kura {
// What `use "x.h"` can see: the header is parsed by clang, as a C compiler
// would with the same `-I` and `-D` flags, and what it declares is described in
// Kura's terms.
//
// Only what a program names is ever used, so a declaration Kura can't express
// (a function that takes a `double*`, say) isn't an error until it is called.
struct CImportConfig {
  std::vector<std::string> include_dirs{};
  std::vector<std::string> defines{};

  static auto Get() -> CImportConfig&;
};

class CHeader {
 public:
  struct Function {
    CSignature signature{};
    // Why it can't be called, or empty.
    std::string problem{};
  };

  struct Constant {
    enum Kind { kInt, kFloat, kString };
    Kind kind = kInt;
    int64_t integer = 0;
    double real = 0;
    std::string text{};
  };

  // A name that can be written where a type is expected: `m.size_t`,
  // `rl.Color`, `c.FILE` (a handle: an opaque struct is only ever a pointer).
  struct Type {
    CType type{};
    std::string problem{};
  };

 private:
  std::string path_;
  std::unordered_map<std::string, Function> functions_{};
  std::unordered_map<std::string, Constant> constants_{};
  std::unordered_map<std::string, Type> types_{};

  explicit CHeader(std::string path) :
    path_(std::move(path)) {}

 public:
  ~CHeader() = default;

  auto GetPath() const -> const std::string& {
    return path_;
  }

  auto FindFunction(const std::string& name) const -> const Function* {
    const auto it = functions_.find(name);
    return it == functions_.end() ? nullptr : &it->second;
  }

  auto FindConstant(const std::string& name) const -> const Constant* {
    const auto it = constants_.find(name);
    return it == constants_.end() ? nullptr : &it->second;
  }

  auto FindType(const std::string& name) const -> const Type* {
    const auto it = types_.find(name);
    return it == types_.end() ? nullptr : &it->second;
  }

  // Parses `header`; null, with `error` saying why, if clang can't.
  static auto Load(const std::string& header, std::string* error) -> std::shared_ptr<CHeader>;
};
}  // namespace kura

#endif  // KURA_C_IMPORT_H
