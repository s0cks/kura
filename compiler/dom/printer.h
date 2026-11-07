#ifndef KURA_DOM_PRINTER_H
#define KURA_DOM_PRINTER_H

#include <ostream>

#include "dom/node.h"

namespace kura::dom {
class Printer : public Visitor {
 private:
  std::ostream& stream_;
  Indent indent_;

 public:
  explicit Printer(std::ostream& stream, const Indent indent = {}) :
    Visitor(),
    stream_(stream),
    indent_(std::move(indent)) {}
  ~Printer() override = default;

  auto VisitNode(Node* node) -> bool override;

 public:
  static inline auto Print(std::ostream& stream, Node* root, const Indent indent = {}) -> bool {
    Printer printer(stream, std::move(indent));
    return root->Accept(&printer);
  }
};
}  // namespace kura::dom

#endif  // KURA_DOM_PRINTER_H
