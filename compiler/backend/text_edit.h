#ifndef KURA_TEXT_EDIT_H
#define KURA_TEXT_EDIT_H

// The text of an input, and what is being done to it: a caret, a selection, and the edits a keyboard makes. It is only
// text: where the letters are on the screen is the runtime's business (see runtime_input.cc), and where the text goes
// when it changes is the program's.
//
// Everything is UTF-8, and a position is a byte offset that is always at the start of a code point, so that no edit
// can cut one in half.
#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace kura::rt {
class TextEdit {
 private:
  std::string text_{};
  size_t caret_ = 0;
  // the other end of the selection: it is empty when this is the caret
  size_t anchor_ = 0;

  static auto IsContinuation(const char c) -> bool {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
  }

  static auto IsWordChar(const char c) -> bool {
    // anything that isn't ASCII punctuation or space is part of a word, so that other scripts are words too
    const auto u = static_cast<unsigned char>(c);
    return u >= 0x80 || (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || u == '_';
  }

  // the nearest start of a code point at or before `pos`
  auto Snap(size_t pos) const -> size_t {
    pos = std::min(pos, text_.size());
    while (pos > 0 && pos < text_.size() && IsContinuation(text_[pos]))
      pos--;
    return pos;
  }

  auto Previous(size_t pos) const -> size_t {
    if (pos == 0)
      return 0;
    pos--;
    while (pos > 0 && IsContinuation(text_[pos]))
      pos--;
    return pos;
  }

  auto Next(size_t pos) const -> size_t {
    if (pos >= text_.size())
      return text_.size();
    pos++;
    while (pos < text_.size() && IsContinuation(text_[pos]))
      pos++;
    return pos;
  }

  auto PreviousWord(size_t pos) const -> size_t {
    while (pos > 0 && !IsWordChar(text_[Previous(pos)]))
      pos = Previous(pos);
    while (pos > 0 && IsWordChar(text_[Previous(pos)]))
      pos = Previous(pos);
    return pos;
  }

  auto NextWord(size_t pos) const -> size_t {
    while (pos < text_.size() && !IsWordChar(text_[pos]))
      pos = Next(pos);
    while (pos < text_.size() && IsWordChar(text_[pos]))
      pos = Next(pos);
    return pos;
  }

  // Moves the caret, and the anchor with it unless the selection is being extended.
  void MoveTo(const size_t pos, const bool extend) {
    caret_ = pos;
    if (!extend)
      anchor_ = pos;
  }

 public:
  TextEdit() = default;
  explicit TextEdit(std::string text) :
    text_(std::move(text)),
    caret_(text_.size()),
    anchor_(text_.size()) {}

  auto GetText() const -> const std::string& {
    return text_;
  }

  auto GetCaret() const -> size_t {
    return caret_;
  }

  auto GetAnchor() const -> size_t {
    return anchor_;
  }

  auto HasSelection() const -> bool {
    return caret_ != anchor_;
  }

  auto SelectionBegin() const -> size_t {
    return std::min(caret_, anchor_);
  }

  auto SelectionEnd() const -> size_t {
    return std::max(caret_, anchor_);
  }

  auto SelectedText() const -> std::string {
    return text_.substr(SelectionBegin(), SelectionEnd() - SelectionBegin());
  }

  // What the text is now, because the program says so. The caret stays where it was, or at the end if the text got
  // shorter than that, and it is not left inside a code point.
  void SetText(std::string text) {
    text_ = std::move(text);
    caret_ = Snap(caret_);
    anchor_ = Snap(anchor_);
  }

  void SetCaret(const size_t pos, const bool extend = false) {
    MoveTo(Snap(pos), extend);
  }

  void SelectAll() {
    anchor_ = 0;
    caret_ = text_.size();
  }

  // Types `utf8` over the selection, or at the caret. Returns whether the text changed.
  auto Insert(const std::string_view utf8) -> bool {
    if (utf8.empty() && !HasSelection())
      return false;
    const auto begin = SelectionBegin();
    text_.replace(begin, SelectionEnd() - begin, utf8);
    MoveTo(begin + utf8.size(), false);
    return true;
  }

  // Deletes the selection, or what is behind the caret: a word of it when `word` is true.
  auto Backspace(const bool word = false) -> bool {
    if (!HasSelection()) {
      if (caret_ == 0)
        return false;
      anchor_ = word ? PreviousWord(caret_) : Previous(caret_);
    }
    return Insert({});
  }

  // Deletes the selection, or what is in front of the caret: a word of it when `word` is true.
  auto Delete(const bool word = false) -> bool {
    if (!HasSelection()) {
      if (caret_ >= text_.size())
        return false;
      anchor_ = word ? NextWord(caret_) : Next(caret_);
    }
    return Insert({});
  }

  // A selection that is collapsed by an arrow goes to its edge, as it does everywhere.
  void Left(const bool extend = false, const bool word = false) {
    if (HasSelection() && !extend)
      return MoveTo(SelectionBegin(), false);
    MoveTo(word ? PreviousWord(caret_) : Previous(caret_), extend);
  }

  void Right(const bool extend = false, const bool word = false) {
    if (HasSelection() && !extend)
      return MoveTo(SelectionEnd(), false);
    MoveTo(word ? NextWord(caret_) : Next(caret_), extend);
  }

  void Home(const bool extend = false) {
    MoveTo(0, extend);
  }

  void End(const bool extend = false) {
    MoveTo(text_.size(), extend);
  }

  // The text a single-line input takes from what was typed or pasted: line breaks and tabs are spaces, and the
  // other control characters are dropped.
  static auto Sanitize(const std::string_view utf8) -> std::string {
    std::string out{};
    out.reserve(utf8.size());
    for (size_t i = 0; i < utf8.size(); i++) {
      const auto c = utf8[i];
      if (c == '\r' && i + 1 < utf8.size() && utf8[i + 1] == '\n')
        continue;
      if (c == '\r' || c == '\n' || c == '\t')
        out.push_back(' ');
      else if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F)
        continue;
      else
        out.push_back(c);
    }
    return out;
  }
};
}  // namespace kura::rt

#endif  // KURA_TEXT_EDIT_H
