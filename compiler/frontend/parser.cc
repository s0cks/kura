#include "parser.h"

#include <fstream>
#include <iostream>
#include <sstream>

#include "frontend/expr_builder.h"
#include "frontend/syntax/diagnostic.h"
#include "frontend/syntax/parser.h"
#include "module.h"

namespace kura {
static inline auto ReadContentsFrom(const std::string& filename, std::string& content) -> bool {
  std::ifstream in(filename, std::ios::binary);
  if (!in)
    return false;
  std::stringstream buffer{};
  buffer << in.rdbuf();
  content = buffer.str();
  return true;
}

auto Parser::ParseModuleFromFile(const std::string filename, Module** m) -> ParseResult {
  std::string source{};
  if (!ReadContentsFrom(filename, source)) {
    std::stringstream ss{};
    ss << "failed to read source from: " << filename;
    return Fail(ss);
  }
  return ParseModuleFromSource(filename, std::move(source), m);
}

auto Parser::ParseModuleFromSource(const std::string& name, std::string text, Module** m) -> ParseResult {
  const auto tree = syntax::ParseText(name, std::move(text));
  if (!tree->IsValid()) {
    // Every problem the parser found, not just the first, each with its line.
    size_t errors = 0;
    for (const auto& diagnostic : tree->GetDiagnostics())
      errors += diagnostic.IsError() ? 1 : 0;
    std::stringstream ss{};
    syntax::Render(ss, tree->GetSource(), tree->GetDiagnostics());
    ss << "error: could not parse " << name << " (" << errors << (errors == 1 ? " syntax error)" : " syntax errors)");
    return Fail(ss);
  }

  expr::ModuleBuilder builder(GetScope());
  try {
    (*m) = builder(*tree);
  } catch (const expr::BuildError& err) {
    std::stringstream ss{};
    ss << name << ":" << err.ToString();
    return Fail(ss);
  }
  return Success();
}
}  // namespace kura
