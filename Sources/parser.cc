#include "parser.h"

#include <iostream>
// clang-format off
#include <antlr4-runtime.h>
// clang-format on

#include "KuraLexer.h"
#include "KuraParser.h"
#include "expr_builder.h"
#include "module.h"

namespace kura {

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

#ifdef KURA_DEBUG
  std::cout << "source:" << std::endl << source << std::endl;
#endif  // KURA_DEBUG

  antlr4::ANTLRInputStream stream(source);
  KuraLexer lexer(&stream);
  antlr4::CommonTokenStream tokens(&lexer);
  KuraParser parser(&tokens);
  auto* tree = parser.source();
#ifdef KURA_DEBUG
  std::cout << "--- Parse Tree ---" << std::endl;
  std::cout << tree->toStringTree(&parser) << std::endl;
  std::cout << std::endl;
#endif  // KURA_DEBUG

  expr::ModuleBuilder builder(GetScope());
  (*m) = builder(tree);
  return Success();
}
}  // namespace kura
