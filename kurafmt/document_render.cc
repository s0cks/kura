#include "document.h"

namespace kura::fmt {
enum class Mode {
  kFlat,
  kBreak,
};

struct PrintItem {
  int indent;
  Mode mode;
  NodePtr node;
};

static inline bool Fits(NodePtr group_node, const std::vector<PrintItem>& render_stack, int max_width,
                        int initial_col) {
  std::vector<PrintItem> stack = render_stack;
  stack.push_back({0, Mode::kFlat, group_node});

  int current_col = initial_col;

  while (!stack.empty()) {
    if (current_col > max_width) {
      return false;
    }

    auto [indent, mode, current_node] = stack.back();
    stack.pop_back();

    bool line_resolved = false;

    std::visit(
        [&](auto&& arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, Nil>) {
            // No width contribution
          } else if constexpr (std::is_same_v<T, Text>) {
            current_col += arg.text.size();
          } else if constexpr (std::is_same_v<T, Line>) {
            if (mode == Mode::kFlat && !arg.hard) {
              if (arg.soft) {
                current_col += 1;
              }
            } else {
              line_resolved = true;
            }
          } else if constexpr (std::is_same_v<T, BreakIf>) {
            stack.push_back({
                indent,
                mode,
                mode == Mode::kFlat ? arg.when_flat : arg.when_break,
            });
          } else if constexpr (std::is_same_v<T, Indent>) {
            stack.push_back({indent + arg.amount, mode, arg.node});
          } else if constexpr (std::is_same_v<T, Concat>) {
            for (auto it = arg.nodes.rbegin(); it != arg.nodes.rend(); ++it) {
              stack.push_back({indent, mode, *it});
            }
          } else if constexpr (std::is_same_v<T, Group>) {
            stack.push_back({indent, mode, arg.node});
          }
        },
        current_node->value);

    if (line_resolved) {
      return current_col <= max_width;
    }
  }

  return current_col <= max_width;
}

auto Render(NodePtr root, const int max_width) -> std::string {
  std::string out{};
  std::vector<PrintItem> stack = {{
      0,
      Mode::kFlat,
      root,
  }};

  int current_col = 0;
  while (!stack.empty()) {
    auto [indent, mode, node] = stack.back();
    stack.pop_back();

    std::visit(
        [&, indent = indent, mode = mode](auto&& arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, Nil>) {
            // do nothing
          } else if constexpr (std::is_same_v<T, Text>) {
            out.append(arg.text);
            current_col += arg.text.size();
          } else if constexpr (std::is_same_v<T, Line>) {
            if (mode == Mode::kFlat && !arg.hard) {
              if (arg.soft) {
                out.append(" ");
                current_col += 1;
              }
            } else {
              out.append("\n");
              out.append(indent, ' ');
              current_col = indent;
            }
          } else if constexpr (std::is_same_v<T, Indent>) {
            stack.push_back({
                indent + arg.amount,
                mode,
                arg.node,
            });
          } else if constexpr (std::is_same_v<T, BreakIf>) {
            stack.push_back({
                indent,
                mode,
                (mode == Mode::kFlat) ? arg.when_flat : arg.when_break,
            });
          } else if constexpr (std::is_same_v<T, Concat>) {
            for (auto it = arg.nodes.rbegin(); it != arg.nodes.rend(); ++it) {
              stack.push_back({
                  indent,
                  mode,
                  *it,
              });
            }
          } else if constexpr (std::is_same_v<T, Group>) {
            Mode next = Fits(arg.node, stack, max_width, current_col) ? Mode::kFlat : Mode::kBreak;
            stack.push_back({
                indent,
                next,
                arg.node,
            });
          }
        },
        node->value);
  }

  return out;
}
}  // namespace kura::fmt
