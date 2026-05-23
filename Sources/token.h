#ifndef KURA_TOKEN_H
#define KURA_TOKEN_H

#include "source_pos.h"
#include <cstdint>

namespace kura {
#define FOR_EACH_TOKEN(V)                                                      \
  V(Let)                                                                       \
  V(LeftBrace)                                                                 \
  V(RightBrace)                                                                \
  V(Plus)                                                                      \
  V(Minus)                                                                     \
  V(Star)                                                                      \
  V(BackSlash)                                                                 \
  V(ForwardSlash)                                                              \
  V(LiteralString)                                                             \
  V(LiteralNumber)                                                             \
  V(LiteralBool)                                                               \
  V(LiteralObject)                                                             \
  V(LiteralList)

struct Token {
  enum Kind : uint64_t {
    // clang-format off
    kInvalidToken,
#define DEFINE_KIND(Name) k##Name,
    FOR_EACH_TOKEN(DEFINE_KIND)
#undef DEFINE_KIND
    kTotalNumberOfTokens,
    // clang-format on
  };

  Kind kind;
  SourcePos start;
  SourcePos end;

  constexpr inline auto IsInvalid() const -> bool {
    return kind == Token::kInvalidToken;
  }

#define DEFINE_KIND_CHECK(Name)                                                \
  constexpr inline auto Is##Name() const -> bool {                             \
    return kind == Token::k##Name;                                             \
  }
  FOR_EACH_TOKEN(DEFINE_KIND_CHECK)
#undef DEFINE_KIND_CHECK
};
} // namespace kura

#endif // KURA_TOKEN_H
