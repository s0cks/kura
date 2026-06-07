#ifndef KURA_COMMON_H
#define KURA_COMMON_H

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>

// clang-format off
#ifndef XXH_INLINE_ALL
#define XXH_INLINE_ALL
#endif  // XXH_INLINE_ALL
#include <xxhash.h>
// clang-format on

#ifndef NOT_IMPLEMENTED

// clang-format off
#define _NOT_IMPLEMENTED(Name) \
  std::cerr << Name << " is not implemented!" << std::endl;

#define NOT_IMPLEMENTED \
  _NOT_IMPLEMENTED(__PRETTY_FUNCTION__)
// clang-format on

#endif  // NOT_IMPLEMENTED

namespace kura {
#define DEFINE_NON_COPYABLE_TYPE(Name)             \
 public:                                           \
  Name(const Name& rhs) = delete;                  \
  Name(Name&& rhs) = delete;                       \
  auto operator=(const Name& rhs)->Name& = delete; \
  auto operator=(Name&& rhs)->Name& = delete;

#define DEFINE_DEFAULT_COPYABLE_TYPE(Name)          \
 public:                                            \
  Name(const Name& rhs) = default;                  \
  Name(Name&& rhs) = default;                       \
  auto operator=(const Name& rhs)->Name& = default; \
  auto operator=(Name&& rhs)->Name& = default;

#define FOR_EACH_VISIT_RESULT(V) \
  V(Continue)                    \
  V(Stop)

class VisitResult {
 public:
  enum Kind {
#define DEFINE_KIND(Name) k##Name,
    FOR_EACH_VISIT_RESULT(DEFINE_KIND)
#undef DEFINE_KIND
  };

  static inline auto ToString(const Kind& rhs) -> std::string_view {
    switch (rhs) {
#define DEFINE_TOSTRING(Name) \
  case Kind::k##Name:         \
    return #Name;
      FOR_EACH_VISIT_RESULT(DEFINE_TOSTRING)
#undef DEFINE_TOSTRING
      default:
        return "Unknown";
    }
  }

  friend auto operator<<(std::ostream& stream, const Kind& rhs) -> std::ostream& {
    return stream << ToString(rhs);
  }

 private:
  Kind kind_;

 public:
  constexpr VisitResult(const Kind kind) :
    kind_(kind) {}
  constexpr VisitResult(const bool rhs) :
    kind_(rhs ? kContinue : kStop) {}
  ~VisitResult() = default;

  constexpr inline auto GetKind() const -> Kind {
    return kind_;
  }

  constexpr auto Is(const Kind rhs) const -> bool {
    return GetKind() == rhs;
  }

#define DEFINE_CHECK(Name)                  \
  constexpr auto Is##Name() const -> bool { \
    return Is(Kind::k##Name);               \
  }
  FOR_EACH_VISIT_RESULT(DEFINE_CHECK)
#undef DEFINE_CHECK

  constexpr operator bool() const {
    return GetKind() == Kind::kContinue;
  }

  constexpr operator Kind() const {
    return GetKind();
  }

  constexpr auto operator==(const VisitResult& rhs) const -> bool {
    return GetKind() == rhs.GetKind();
  }

  constexpr auto operator!=(const VisitResult& rhs) const -> bool {
    return GetKind() != rhs.GetKind();
  }

  constexpr auto operator<(const VisitResult& rhs) const -> bool {
    return GetKind() < rhs.GetKind();
  }

  constexpr auto operator>(const VisitResult& rhs) const -> bool {
    return GetKind() > rhs.GetKind();
  }

  friend auto operator<<(std::ostream& stream, const VisitResult& rhs) -> std::ostream& {
    return stream << "VisitResult(kind=" << rhs.GetKind() << ")";
  }

 public:
  static inline constexpr auto New(const Kind kind) -> VisitResult {
    return kind;
  }

#define DEFINE_NEW(Name)                               \
  static inline constexpr auto Name() -> VisitResult { \
    return k##Name;                                    \
  }
  FOR_EACH_VISIT_RESULT(DEFINE_NEW)
#undef DEFINE_NEW
};

struct Indent {
  std::string value{};
  uint8_t length;

  Indent(const uint8_t n = 0) :
    length(n) {
    value.resize(length * 2, ' ');
  }
  Indent(const Indent& rhs) = default;
  Indent(Indent&& rhs) = default;
  ~Indent() = default;

  inline auto Increment(const int8_t n = 1) -> Indent& {
    if (std::numeric_limits<uint8_t>::max() - length < n) {
      length = std::numeric_limits<uint8_t>::max();
    } else {
      length += n;
    }
    return resize();
  }

  inline auto Decrement(const int8_t n = 1) -> Indent& {
    if (length < n) {
      length = 0;
    } else {
      length -= n;
    }
    return resize();
  }

  auto operator++() -> Indent& {
    return Increment();
  }

  auto operator--() -> Indent& {
    return Decrement();
  }

  operator std::string_view() const {
    return value.data();
  }

  auto operator+(const int rhs) const -> Indent {
    return value.length() + rhs;
  }

  auto operator=(Indent&& rhs) -> Indent& = default;
  auto operator=(const Indent& rhs) -> Indent& = default;

  friend auto operator<<(std::ostream& stream, const Indent& rhs) -> std::ostream& {
    return stream << rhs.value;
  }

 private:
  inline auto resize() -> Indent& {
    value.resize(length * 2, ' ');
    return *this;
  }
};

static inline auto operator+(const int lhs, const Indent& rhs) -> Indent {
  return rhs.length + rhs;
}

template <const int64_t Size>
class TemplateIndentScope {
  DEFINE_NON_COPYABLE_TYPE(TemplateIndentScope<Size>);

 private:
  Indent& indent_;

 public:
  explicit TemplateIndentScope(Indent& indent) :
    indent_(indent) {
    indent_.Increment(Size);
  }
  ~TemplateIndentScope() {
    indent_.Decrement(Size);
  }

  operator Indent&() {
    return indent_;
  }

  operator const Indent&() const {
    return indent_;
  }
};

using DefaultIndentScope = TemplateIndentScope<1>;
using IndentScope = DefaultIndentScope;
using NegativeDefaultIndentScope = TemplateIndentScope<-1>;

template <typename T>
class TemplateVisitor {
 public:
  TemplateVisitor() = default;
  virtual ~TemplateVisitor() = default;
  virtual auto Visit(T* rhs) -> VisitResult = 0;
};

template <typename T>
class TemplatePointerVisitor {
 public:
  TemplatePointerVisitor() = default;
  virtual ~TemplatePointerVisitor() = default;
  virtual auto Visit(T** rhs) -> VisitResult = 0;
};

template <typename T>
concept HasToString = requires(T value) {
  { value.ToString() } -> std::convertible_to<std::string>;
};
}  // namespace kura

#endif  // KURA_COMMON_H
