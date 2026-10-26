#include "parser.h"

#include <iostream>
// clang-format off
#include <antlr4-runtime.h>
// clang-format on

#include "KuraLexer.h"
#include "KuraParser.h"
#include "frontend/expr_builder.h"
#include "module.h"

namespace kura {
// Collects syntax errors instead of printing them and carrying on with a
// half-parsed tree.
class SyntaxErrorCollector : public antlr4::BaseErrorListener {
 private:
  std::stringstream errors_{};
  size_t count_ = 0;

 public:
  SyntaxErrorCollector() = default;
  ~SyntaxErrorCollector() override = default;

  auto HasErrors() const -> bool {
    return count_ > 0;
  }

  auto GetErrors() const -> std::string {
    return errors_.str();
  }

  void syntaxError(antlr4::Recognizer* recognizer, antlr4::Token* offending, size_t line, size_t column,
                   const std::string& msg, std::exception_ptr e) override {
    if (count_++ > 0)
      errors_ << std::endl;
    errors_ << line << ":" << column << ": " << msg;
  }
};

static inline auto ReadContentsFrom(const std::string filename, std::string& content) -> bool {
  FILE* file = fopen(filename.data(), "r");
  if (!file)
    return false;
  fseek(file, 0, SEEK_END);
  const auto size = ftell(file);
  content.resize(size);
  rewind(file);
  fread(content.data(), sizeof(char), size, file);
  fclose(file);
  return content.length() >= size;
}

auto Parser::ParseModuleFromFile(const std::string filename, Module** m) -> ParseResult {
  std::string source{};
  if (!ReadContentsFrom(filename, source)) {
    std::stringstream ss{};
    ss << "failed to read source from: " << filename;
    return Fail(ss);
  }

  SyntaxErrorCollector errors{};
  antlr4::ANTLRInputStream stream(source);
  KuraLexer lexer(&stream);
  lexer.removeErrorListeners();
  lexer.addErrorListener(&errors);
  antlr4::CommonTokenStream tokens(&lexer);
  KuraParser parser(&tokens);
  parser.removeErrorListeners();
  parser.addErrorListener(&errors);
  auto* tree = parser.source();
  if (errors.HasErrors())
    return Fail(filename + ": syntax error(s):\n" + errors.GetErrors());

  expr::ModuleBuilder builder(GetScope());
  try {
    (*m) = builder(tree);
  } catch (const expr::BuildError& err) {
    std::stringstream ss{};
    ss << filename << ":" << err.ToString();
    return Fail(ss);
  }
  return Success();
}
}  // namespace kura
