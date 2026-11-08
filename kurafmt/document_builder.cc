#ifndef KURA_DOCUMENT_BUILDER_H
#define KURA_DOCUMENT_BUILDER_H

#include "document.h"
#include "frontend/syntax/ast.h"

namespace kura::fmt {
auto Build(syntax::Node* node) -> NodePtr {
  // Doc LowerToDoc(const Node* node) {
  //     if (auto* call = dynamic_cast<const CallNode*>(node)) {
  //         std::vector<Doc> inner;
  //         for (size_t i = 0; i < call->args.size(); ++i) {
  //             inner.push_back(LowerToDoc(call->args[i].get()));
  //             if (i + 1 < call->args.size()) {
  //                 inner.push_back(make_text(","));
  //                 inner.push_back(make_line()); // Break line if args overflow line width
  //             }
  //         }
  //
  //         return make_group(make_concat({
  //             make_text(call->callee),
  //             make_text("("),
  //             make_indent(4, make_concat({ make_line(), make_concat(inner) })),
  //             make_line(),
  //             make_text(")")
  //         }));
  //     }
  //     // Handle binary ops, blocks, pipelines etc...
  //     return std::make_shared<DocNode>(Nil{});
  // }
  return nullptr;
}
}  // namespace kura::fmt

#endif  // KURA_DOCUMENT_BUILDER_H
