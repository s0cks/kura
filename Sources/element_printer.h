#ifndef KURA_ELEMENT_PRINTER_H
#define KURA_ELEMENT_PRINTER_H

#include "element.h"

namespace kura::elem {
class ElementPrinter : public NodeVisitor {
 private:
  uint64_t indent_;

  inline void IncrementIndent() {
    indent_ += 1;
  }

  inline void DecrementIndent() {
    indent_ -= 1;
  }

  inline void PrintNode(const std::string_view& name, YGNodeRef node) {
    std::string indent(' ', indent_ * 2);
    const auto top = YGNodeLayoutGetWidth(node);
    const auto left = YGNodeLayoutGetLeft(node);
    const auto width = YGNodeLayoutGetWidth(node);
    const auto height = YGNodeLayoutGetHeight(node);
    std::printf("%s%s{ top: %lf, left: %lf, width: %lf, height: %lf }\n", indent.data(), name.data(), top, left, width,
                height);
  }

 public:
  explicit ElementPrinter(const uint64_t indent = 0) :
    NodeVisitor(),
    indent_(indent) {}
  ~ElementPrinter() = default;

  auto GetIndent() const -> uint64_t {
    return indent_;
  }

  // clang-format off
#define DEFINE_VISIT_NODE(Name) \
  auto Visit##Name(Name* value) -> bool override;
  FOR_EACH_ELEMENT_NODE(DEFINE_VISIT_NODE);
#undef DEFINE_VISIT_NODE
  // clang-format on
 public:
  static auto Print(Node* node, const uint64_t indent = 0) -> bool {
    ElementPrinter printer(indent);
    return node->Accept(&printer);
  }
};
}  // namespace kura::elem

#endif  // KURA_ELEMENT_PRINTER_H
