#ifndef KURA_SYNTAX_SOURCE_H
#define KURA_SYNTAX_SOURCE_H

#include <algorithm>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "frontend/span.h"

namespace kura::syntax {

// A zero-based line and column. Columns count bytes; use
// SourceFile::GetUtf16Column when talking to an LSP client.
struct Position {
  uint32_t line = 0;
  uint32_t column = 0;

  constexpr auto operator==(const Position& rhs) const -> bool {
    return line == rhs.line && column == rhs.column;
  }

  constexpr auto operator!=(const Position& rhs) const -> bool {
    return !(*this == rhs);
  }

  friend auto operator<<(std::ostream& stream, const Position& rhs) -> std::ostream& {
    // humans count from one
    return stream << (rhs.line + 1) << ":" << (rhs.column + 1);
  }
};

// The text of one source file plus a table of where every line starts.
// Spans in tokens, AST nodes and diagnostics all index into `GetText()`.
class SourceFile {
 private:
  std::string name_;
  std::string text_{};
  std::vector<uint32_t> line_starts_{};

  void IndexLines() {
    line_starts_.clear();
    line_starts_.push_back(0);
    for (uint32_t idx = 0; idx < text_.size(); idx++) {
      if (text_[idx] == '\n')
        line_starts_.push_back(idx + 1);
    }
  }

 public:
  SourceFile() = default;
  SourceFile(std::string name, std::string text) :
    name_(std::move(name)),
    text_(std::move(text)) {
    IndexLines();
  }
  ~SourceFile() = default;

  auto GetName() const -> const std::string& {
    return name_;
  }

  auto GetText() const -> const std::string& {
    return text_;
  }

  auto GetSize() const -> uint32_t {
    return static_cast<uint32_t>(text_.size());
  }

  auto GetNumberOfLines() const -> uint32_t {
    return static_cast<uint32_t>(line_starts_.size());
  }

  auto GetText(const Span& span) const -> std::string_view {
    const auto begin = std::min<size_t>(span.begin, text_.size());
    const auto end = std::min<size_t>(span.end, text_.size());
    return std::string_view(text_).substr(begin, end - begin);
  }

  // Maps a byte offset to a line and column. Offsets past the end clamp to
  // the end of the file.
  auto GetPosition(uint32_t offset) const -> Position {
    offset = std::min(offset, GetSize());
    const auto next = std::upper_bound(line_starts_.begin(), line_starts_.end(), offset);
    const auto line = static_cast<uint32_t>(std::distance(line_starts_.begin(), next)) - 1;
    return {line, offset - line_starts_[line]};
  }

  auto GetOffset(const Position& pos) const -> uint32_t {
    if (pos.line >= line_starts_.size())
      return GetSize();
    return std::min(line_starts_[pos.line] + pos.column, GetSize());
  }

  // The span of a line, without its line terminator.
  auto GetLineSpan(const uint32_t line) const -> Span {
    if (line >= line_starts_.size())
      return {GetSize(), GetSize()};
    auto end = line + 1 < line_starts_.size() ? line_starts_[line + 1] : GetSize();
    if (end > line_starts_[line] && text_[end - 1] == '\n')
      end--;
    if (end > line_starts_[line] && text_[end - 1] == '\r')
      end--;
    return {line_starts_[line], end};
  }

  // LSP positions count UTF-16 code units, not bytes.
  auto GetUtf16Column(const uint32_t offset) const -> uint32_t {
    const auto pos = GetPosition(offset);
    const auto start = line_starts_[pos.line];
    uint32_t units = 0;
    for (uint32_t idx = start; idx < offset && idx < text_.size(); idx++) {
      const auto c = static_cast<unsigned char>(text_[idx]);
      if ((c & 0xC0) == 0x80)
        continue;                    // a continuation byte
      units += (c >= 0xF0) ? 2 : 1;  // a four byte sequence is a surrogate pair
    }
    return units;
  }
};
}  // namespace kura::syntax

#endif  // KURA_SYNTAX_SOURCE_H
