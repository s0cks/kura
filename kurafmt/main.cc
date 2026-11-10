#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <print>
#include <string>

#include "compiler/frontend/syntax/parser.h"
#include "config.h"
#include "document.h"
#include "formatter.h"
#include "frontend/parser.h"

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

auto main(int argc, char** argv) -> int {
  using namespace kura;
  if (argc < 2) {
    std::cerr << "usage: kurac <file> [<c-source>.c...] [--emit-llvm] [--emit-obj <file.o>] [--emit-header <file.h>] "
                 "[--no-opt] [--dump-dom] [-I<dir>] [-D<name>] [-L<dir>] [-l<lib>] [--run <function> [args...]]"
              << std::endl;
    return EXIT_FAILURE;
  }

  fmt::Config config{};
  fmt::ParseConfig(config);
  const auto tree = ParseTree(argv[1]);
  if (!tree)
    return EXIT_FAILURE;

  fmt::Formatter formatter(std::cout, std::move(config));
  if (!formatter.Format(tree.get()))
    return EXIT_FAILURE;

  return EXIT_SUCCESS;
}
