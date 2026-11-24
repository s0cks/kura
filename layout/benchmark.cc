#include <benchmark/benchmark.h>

#include <vector>

#include "layout/layout.h"

using namespace kura::layout;

namespace {

// 1. Flat vertical list (like a chat log, inventory, or scroll container)
struct FlatListTree {
  std::vector<Node> nodes{};
  std::vector<Box> boxes{};
  NodeId root = kNoNode;

  explicit FlatListTree(const size_t count) {
    nodes.reserve(count + 2);

    Style root_style{};
    root_style.direction = Direction::kColumn;
    root_style.padding = Edges::All(Length::Px(16));
    root_style.gap = Length::Px(8);
    root = AddNode(&nodes, kNoNode, root_style);

    Style row_style{};
    row_style.direction = Direction::kRow;
    row_style.height = Length::Px(36);
    row_style.padding = Edges{Length::Px(4), Length::Px(12), Length::Px(4), Length::Px(12)};
    row_style.gap = Length::Px(8);
    row_style.align = Align::kCenter;

    Style badge_style{};
    badge_style.width = Length::Px(24);
    badge_style.height = Length::Px(24);

    Style label_style{};
    label_style.grow = 1.0f;
    label_style.basis = Length::Px(0);
    label_style.height = Length::Px(20);

    for (size_t i = 0; i < count; ++i) {
      const auto row = AddNode(&nodes, root, row_style);
      AddNode(&nodes, row, badge_style);
      AddNode(&nodes, row, label_style);
    }
    boxes.resize(nodes.size());
  }
};

// 2. Deeply nested auto-sized flex containers (stresses cache hit rate & O(2^d) blowup prevention)
struct DeepTree {
  std::vector<Node> nodes{};
  std::vector<Box> boxes{};
  NodeId root = kNoNode;

  explicit DeepTree(const size_t depth) {
    Style root_style{};
    root_style.direction = Direction::kColumn;
    root_style.align = Align::kStart;  // forces intrinsic sizing down the tree
    root = AddNode(&nodes, kNoNode, root_style);

    NodeId parent = root;
    for (size_t d = 0; d < depth; ++d) {
      Style box_style{};
      box_style.direction = (d % 2 == 0) ? Direction::kRow : Direction::kColumn;
      box_style.padding = Edges::All(Length::Px(4));
      box_style.gap = Length::Px(4);
      // Leave width/height/basis as Auto to force intrinsic measurement passes
      const auto next = AddNode(&nodes, parent, box_style);

      // Add a sibling leaf at every level
      Style leaf_style{};
      leaf_style.width = Length::Px(40);
      leaf_style.height = Length::Px(20);
      AddNode(&nodes, parent, leaf_style);

      parent = next;
    }

    Style tip_style{};
    tip_style.width = Length::Px(60);
    tip_style.height = Length::Px(24);
    AddNode(&nodes, parent, tip_style);

    boxes.resize(nodes.size());
  }
};

// 3. Wrapping chip/tag flow container (exercises multi-line collection & cross-axis stretching)
struct WrappingTree {
  std::vector<Node> nodes{};
  std::vector<Box> boxes{};
  NodeId root = kNoNode;

  explicit WrappingTree(const size_t item_count) {
    nodes.reserve(item_count + 1);

    Style root_style{};
    root_style.direction = Direction::kRow;
    root_style.wrap = true;
    root_style.gap = Length::Px(6);
    root_style.padding = Edges::All(Length::Px(12));
    root = AddNode(&nodes, kNoNode, root_style);

    for (size_t i = 0; i < item_count; ++i) {
      Style chip{};
      chip.width = Length::Px(48.0f + static_cast<float>((i * 17) % 64));
      chip.height = Length::Px(28);
      AddNode(&nodes, root, chip);
    }
    boxes.resize(nodes.size());
  }
};

// 4. Measured text leaves (simulates Harfbuzz / glyph width & line-wrap queries)
struct MeasuredTextTree {
  std::vector<Node> nodes{};
  std::vector<Box> boxes{};
  NodeId root = kNoNode;

  explicit MeasuredTextTree(const size_t count) {
    nodes.reserve(count * 2 + 1);

    Style root_style{};
    root_style.direction = Direction::kColumn;
    root_style.padding = Edges::All(Length::Px(16));
    root_style.gap = Length::Px(10);
    root = AddNode(&nodes, kNoNode, root_style);

    Style text_style{};
    text_style.shrink = 1.0f;

    for (size_t i = 0; i < count; ++i) {
      AddNode(&nodes, root, text_style, /*measured=*/true);
    }
    boxes.resize(nodes.size());
  }

  static auto MeasureText(void*, const NodeId id, const Scalar max_width) -> Size {
    const float natural_w = 180.0f + static_cast<float>((id * 31) % 320);
    if (!IsDefined(max_width) || natural_w <= max_width) {
      return Size{natural_w, 20.0f};
    }
    const float lines = std::ceil(natural_w / std::max(max_width, 1.0f));
    return Size{max_width, lines * 20.0f};
  }
};

}  // namespace

static void BM_Layout_FlatList(benchmark::State& state) {
  const auto count = static_cast<size_t>(state.range(0));
  FlatListTree tree(count);
  Engine engine{};
  const Size viewport{1280.0f, 720.0f};

  for (auto _ : state) {
    engine.Run(tree.nodes, tree.root, viewport, tree.boxes);
    benchmark::DoNotOptimize(tree.boxes.data());
  }
  state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(tree.nodes.size()));
  state.counters["computes"] = static_cast<double>(engine.GetNumberOfComputes());
}

static void BM_Layout_DeepNesting(benchmark::State& state) {
  const auto depth = static_cast<size_t>(state.range(0));
  DeepTree tree(depth);
  Engine engine{};
  const Size viewport{1280.0f, 720.0f};

  for (auto _ : state) {
    engine.Run(tree.nodes, tree.root, viewport, tree.boxes);
    benchmark::DoNotOptimize(tree.boxes.data());
  }
  state.counters["nodes"] = static_cast<double>(tree.nodes.size());
  state.counters["computes"] = static_cast<double>(engine.GetNumberOfComputes());
}

static void BM_Layout_WrappingFlow(benchmark::State& state) {
  const auto count = static_cast<size_t>(state.range(0));
  WrappingTree tree(count);
  Engine engine{};
  const Size viewport{1024.0f, 768.0f};

  for (auto _ : state) {
    engine.Run(tree.nodes, tree.root, viewport, tree.boxes);
    benchmark::DoNotOptimize(tree.boxes.data());
  }
  state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(tree.nodes.size()));
  state.counters["computes"] = static_cast<double>(engine.GetNumberOfComputes());
}

static void BM_Layout_MeasuredText(benchmark::State& state) {
  const auto count = static_cast<size_t>(state.range(0));
  MeasuredTextTree tree(count);
  Engine engine{};
  const Size viewport{640.0f, 800.0f};

  for (auto _ : state) {
    engine.Run(tree.nodes, tree.root, viewport, tree.boxes, &MeasuredTextTree::MeasureText, nullptr);
    benchmark::DoNotOptimize(tree.boxes.data());
  }
  state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(tree.nodes.size()));
  state.counters["computes"] = static_cast<double>(engine.GetNumberOfComputes());
}

BENCHMARK(BM_Layout_FlatList)->Arg(100)->Arg(1000)->Arg(5000)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_Layout_DeepNesting)->Arg(4)->Arg(8)->Arg(12)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_Layout_WrappingFlow)->Arg(200)->Arg(1000)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_Layout_MeasuredText)->Arg(100)->Arg(1000)->Unit(benchmark::kMicrosecond);
