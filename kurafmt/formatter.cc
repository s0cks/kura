#include "formatter.h"

#include "compiler/frontend/syntax/parser.h"
#include "config.h"
#include "document.h"
#include "frontend/parser.h"

namespace kura::fmt {
auto Formatter::Format(syntax::SyntaxTree* tree) -> bool {
  const auto doc = fmt::Lower(config_, *tree->GetRoot());
  if (!doc)
    return false;

  stream() << "prettier:\n";
  stream() << fmt::Render(doc, 90);
  stream() << "\n";
  stream() << std::endl;
  return true;
}
}  // namespace kura::fmt
