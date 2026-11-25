#ifndef KURA_SPAN_H
#define KURA_SPAN_H

#include <algorithm>
#include <cstdint>
#include <jansson.h>
#include <ostream>

namespace kura {
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

// A secondary span: "this `{` is never closed".
struct Label {
  Span span;
  std::string message;
};
}  // namespace kura

#endif  // KURA_SPAN_H
