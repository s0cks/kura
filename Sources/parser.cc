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

auto Parser::ParseModuleFromFile(const std::string filename, Module** m) -> bool {
  std::string source{};
  if (!ReadContentsFrom(filename, source)) {
    std::cerr << "failed to read source from: " << filename;
    return false;
  }
  std::cout << "source:" << std::endl << source << std::endl;

  antlr4::ANTLRInputStream stream(source);
  KuraLexer lexer(&stream);
  antlr4::CommonTokenStream tokens(&lexer);
  KuraParser parser(&tokens);
  auto* tree = parser.source();
  std::cout << "--- Parse Tree ---" << std::endl;
  std::cout << tree->toStringTree(&parser) << std::endl;
  std::cout << std::endl;

  expr::ExprBuilder expr_builder(GetScope());
  (*m) = std::any_cast<Module*>(expr_builder.visit(tree));
  return true;
}
}  // namespace kura
