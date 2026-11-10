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

static inline bool Fits(NodePtr node, int max_width, int initial_col) {
  struct FitsItem {
    Mode mode;
    NodePtr node;
  };

  std::vector<FitsItem> stack = {{Mode::kFlat, node}};
  int current_col = initial_col;

  while (!stack.empty()) {
    if (current_col > max_width) {
      return false;
    }

    auto [mode, current_node] = stack.back();
    stack.pop_back();

    std::visit(
        [&](auto&& arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, Nil>) {
            // No width contribution
          } else if constexpr (std::is_same_v<T, Text>) {
            current_col += arg.text.size();
          } else if constexpr (std::is_same_v<T, Line>) {
            if (arg.hard) {
              // A hard line cannot fit flat on a single line
              current_col = max_width + 1;
            } else if (mode == Mode::kFlat) {
              current_col += 1;  // Renders as a single space
            } else {
              // Renders as a newline, meaning everything up to here fit
              current_col = max_width + 1;
            }
          } else if constexpr (std::is_same_v<T, Indent>) {
            stack.push_back({mode, arg.node});
          } else if constexpr (std::is_same_v<T, Concat>) {
            for (auto it = arg.nodes.rbegin(); it != arg.nodes.rend(); ++it) {
              stack.push_back({mode, *it});
            }
          } else if constexpr (std::is_same_v<T, Group>) {
            // Nested groups inside a flat check inherit the flat context
            stack.push_back({mode, arg.node});
          }
        },
        current_node->value);
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
              out.append(" ");
              current_col += 1;
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
          } else if constexpr (std::is_same_v<T, Concat>) {
            for (auto it = arg.nodes.rbegin(); it != arg.nodes.rend(); ++it) {
              stack.push_back({
                  indent,
                  mode,
                  *it,
              });
            }
          } else if constexpr (std::is_same_v<T, Group>) {
            Mode next = mode;
            if (mode == Mode::kFlat)
              next = Fits(arg.node, max_width, current_col) ? Mode::kFlat : Mode::kBreak;

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
