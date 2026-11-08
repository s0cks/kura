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

static inline bool Fits(NodePtr node, int remaining) {
  if (remaining < 0)
    return false;

  struct Visitor {
    int& width;

    bool operator()(Nil) {
      return true;
    }

    bool operator()(Text t) {
      width -= t.text.size();
      return width >= 0;
    }

    bool operator()(Line l) {
      if (l.hard)
        return false;
      width -= 1;
      return width >= 0;
    }

    bool operator()(Indent i) {
      return std::visit(*this, i.node->value);
    }

    bool operator()(Concat c) {
      for (auto& d : c.nodes) {
        if (!std::visit(*this, d->value))
          return false;
      }
      return true;
    }

    bool operator()(Group g) {
      return std::visit(*this, g.node->value);
    }
  };

  Visitor vis{remaining};
  return std::visit(vis, node->value);
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
              next = Fits(arg.node, max_width - current_col) ? Mode::kFlat : Mode::kBreak;
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
