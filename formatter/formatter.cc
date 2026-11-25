#include "formatter/formatter.h"

#include "compiler/frontend/parser.h"
#include "compiler/frontend/syntax/parser.h"
#include "formatter/config.h"
#include "formatter/document.h"

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
