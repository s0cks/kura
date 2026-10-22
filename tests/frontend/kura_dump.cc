// Parses .kura/.kui files and prints the resulting module: types, top-level
// bindings and every function as an s-expression. Used to produce the golden
// files next to this file.
//
//   kura_dump file.kui...
#include <iostream>

#include "frontend/expr_printer.h"
#include "frontend/parser.h"
#include "local_scope.h"
#include "module.h"

using namespace kura;
using namespace kura::expr;

static void PrintDirectives(const DirectiveList& directives) {
  for (const auto d : directives)
    std::cout << "@" << d->name << " ";
}

int main(int argc, char** argv) {
  Type::Init();
  int rc = 0;
  for (int i = 1; i < argc; i++) {
    Parser parser(LocalScope::New());
    Module* m = nullptr;
    const auto result = parser.ParseModuleFromFile(argv[i], &m);
    if (!result) {
      std::cout << "ERROR " << result.message << std::endl;
      rc = 1;
      continue;
    }

    for (const auto type : m->types) {
      PrintDirectives(type->GetDirectives());
      std::cout << type->ToString() << std::endl;
    }
    for (const auto binding : m->bindings)
      std::cout << ExprPrinter::ToString(binding) << std::endl;
    for (const auto func : m->functions) {
      PrintDirectives(func->GetDirectives());
      std::cout << "fn " << func->GetName()->GetValue() << "(";
      const auto& params = func->GetParameters();
      for (size_t p = 0; p < params.size(); p++)
        std::cout << (p ? ", " : "") << params[p].local->GetName()->GetValue() << ": " << params[p].type->ToString();
      std::cout << ")";
      if (func->GetReturnType())
        std::cout << ": " << func->GetReturnType()->ToString();
      std::cout << " = " << ExprPrinter::ToString(func->GetBody()) << std::endl;
    }
  }
  return rc;
}
