#ifndef KURA_SYNTAX_SOURCE_H
#define KURA_SYNTAX_SOURCE_H

#include <algorithm>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

// The syntax layer (source, tokens, lexer, AST, parser, diagnostics) depends
// on the standard library only. That keeps it cheap to unit test, to fuzz, and
// to link from the LSP (CGo) without dragging in LLVM, Clang or the Kura heap.
namespace kura::syntax {
// A half-open byte range [begin, end) into a SourceFile.
struct Span {
  uint32_t begin = 0;
  uint32_t end = 0;

  constexpr Span() = default;
  constexpr Span(const uint32_t b, const uint32_t e) :
    begin(b),
    end(e) {}

  constexpr auto length() const -> uint32_t {
    return end - begin;
  }

  constexpr auto empty() const -> bool {
    return begin == end;
  }

  constexpr auto Contains(const uint32_t offset) const -> bool {
    return offset >= begin && offset < end;
  }

  // Like Contains, but an offset right after the last byte still counts: this
  // is what an editor cursor sitting at the end of an identifier means.
  constexpr auto Touches(const uint32_t offset) const -> bool {
    return offset >= begin && offset <= end;
  }

  // The smallest span that covers both.
  constexpr auto Join(const Span& rhs) const -> Span {
    return {std::min(begin, rhs.begin), std::max(end, rhs.end)};
  }

  constexpr auto operator==(const Span& rhs) const -> bool {
    return begin == rhs.begin && end == rhs.end;
  }

  constexpr auto operator!=(const Span& rhs) const -> bool {
    return !(*this == rhs);
  }

  friend auto operator<<(std::ostream& stream, const Span& rhs) -> std::ostream& {
    return stream << "[" << rhs.begin << ", " << rhs.end << ")";
  }
};

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
  std::string text_;
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
        continue;  // a continuation byte
      units += (c >= 0xF0) ? 2 : 1;  // a four byte sequence is a surrogate pair
    }
    return units;
  }
};
}  // namespace kura::syntax

#endif  // KURA_SYNTAX_SOURCE_H
