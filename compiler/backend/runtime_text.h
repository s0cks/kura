#ifndef KURA_RUNTIME_TEXT_H
#define KURA_RUNTIME_TEXT_H

// Text, as the runtime lays it out: what layout measures a text with, and what drawing places its lines with, so that
// what is drawn is what was made room for. The runtime's other parts that draw text, like tooltips, use it as well.
#include <string>
#include <vector>

namespace kura::rt {
// One line of a text that has been wrapped, and how big it is.
struct TextLine {
  std::string text{};
  float width = 0;
  float height = 0;
};

// A text's lines: all of it on one line when it fits in `max_width`, and otherwise broken at word boundaries. A word
// longer than the line gets a line of its own and overflows it. The renderer in use measures the text.
auto WrapText(const std::string& text, float size, const std::string& family, float max_width) -> std::vector<TextLine>;
}  // namespace kura::rt

#endif  // KURA_RUNTIME_TEXT_H
