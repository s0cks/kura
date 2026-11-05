#ifndef KURA_BACKEND_LAYOUT_PLAN_H
#define KURA_BACKEND_LAYOUT_PLAN_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "dom/node.h"
#include "layout/layout.h"

namespace kura {
// The layout of one DOM tree, worked out at compile time as far as it can be: every element gets a node
// id (its pre-order index) and a row in a table of layout nodes, built from the properties that are
// constant. The table is emitted as constant data; the runtime copies it and runs the layout kernel
// over it (see runtime_layout.cc), so what is left for run time is only what depends on run time: the
// size of the viewport and the size of the text.
//
// `#fragment`s are in the table too, as `display: contents` nodes, so ids stay one-to-one with
// elements. Dynamic (`for`/`if`) and bind nodes are not: they are rendered as trees of their own, which
// are laid out on their own and don't take part in this tree's layout yet.
class LayoutPlan {
 private:
  uint64_t index_;
  std::vector<layout::Node> nodes_{};
  std::unordered_map<const dom::Node*, layout::NodeId> ids_{};
  std::unordered_map<layout::NodeId, double> font_sizes_{};

  explicit LayoutPlan(const uint64_t index) :
    index_(index) {}

  auto Add(dom::Node* node, dom::Node* root, layout::NodeId parent, std::string* error) -> bool;
  auto Convert(dom::Node* node, layout::Style* style, layout::NodeId id, std::string* error) -> bool;

 public:
  static constexpr double kDefaultFontSize = 16.0;

  // Builds the plan for the tree under `root`. Returns null, and says why in `error`, when a layout
  // property can't be made into a constant.
  // The layout property a DOM property is, if it is one: `width`, `gap`, `justify`...
  static auto PropertyOf(std::string_view name) -> std::optional<layout::Property>;

  static auto Build(dom::Node* root, std::string* error) -> std::shared_ptr<LayoutPlan>;

  // A number that's unique to this plan in the process: it names the plan's table in generated code.
  inline auto GetIndex() const -> uint64_t {
    return index_;
  }

  inline auto GetNumberOfNodes() const -> uint64_t {
    return nodes_.size();
  }

  inline auto GetNodes() const -> const std::vector<layout::Node>& {
    return nodes_;
  }

  // The table as the bytes that go into the generated code.
  auto GetBytes() const -> std::vector<uint8_t>;

  inline auto FindId(const dom::Node* node) const -> std::optional<layout::NodeId> {
    const auto it = ids_.find(node);
    return it == ids_.end() ? std::nullopt : std::optional<layout::NodeId>(it->second);
  }

  inline auto GetFontSize(const layout::NodeId id) const -> double {
    const auto it = font_sizes_.find(id);
    return it == font_sizes_.end() ? kDefaultFontSize : it->second;
  }
};
}  // namespace kura

#endif  // KURA_BACKEND_LAYOUT_PLAN_H
