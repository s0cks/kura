// kura-syntax-dump: parse a Kura source file and show what the parser made of it.
//
//   kura-syntax-dump [--tokens] [--ast] [--spans] [--color] FILE...
//
// Diagnostics are always printed (to stderr). The exit status is 1 when any
// file has errors, so the grammar test script can use it as a pass/fail check.
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

#include "frontend/syntax/ast_printer.h"
#include "frontend/syntax/parser.h"

int main(int argc, char** argv) {
  using namespace kura::syntax;
  bool tokens = false;
  bool ast = false;
  bool spans = false;
  bool color = false;
  int status = 0;
  int files = 0;
  for (int idx = 1; idx < argc; idx++) {
    const std::string arg = argv[idx];
    if (arg == "--tokens") {
      tokens = true;
    } else if (arg == "--ast") {
      ast = true;
    } else if (arg == "--spans") {
      spans = true;
    } else if (arg == "--color") {
      color = true;
    } else if (arg.rfind("--", 0) == 0) {
      std::cerr << "unknown option " << arg << "\n";
      return 2;
    } else {
      files++;
      std::ifstream in(arg, std::ios::binary);
      if (!in) {
        std::cerr << "can't read " << arg << "\n";
        return 2;
      }
      std::stringstream buffer{};
      buffer << in.rdbuf();
      const auto tree = ParseText(arg, buffer.str());
      if (tokens)
        PrintTokens(std::cout, *tree);
      if (ast)
        PrintTree(std::cout, *tree, PrintOptions{.spans = spans});
      RenderOptions options{};
      options.color = color;
      Render(std::cerr, tree->GetSource(), tree->GetDiagnostics(), options);
      if (!tree->IsValid())
        status = 1;
    }
  }
  if (files == 0) {
    std::cerr << "usage: kura-syntax-dump [--tokens] [--ast] [--spans] [--color] FILE...\n";
    return 2;
  }
  return status;
}
