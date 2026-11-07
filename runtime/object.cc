#include "object.h"

#include <cstdint>
#include <iostream>
#include <optional>
#include <sstream>
#include <string_view>

#include "common.h"

namespace kura {
static None* kNone = nullptr;

void None::Init() {
  kNone = None::New();
}

auto None::Get() -> None* {
  return kNone;
}

auto None::ToString() const -> std::string {
  return "none";
}

static Bool* kTrue = nullptr;
static Bool* kFalse = nullptr;

void Bool::Init() {
  // TODO(@s0cks): check kTrue and kFalse values before initializing
  kTrue = Bool::New(true);
  kFalse = Bool::New(false);
}

auto Bool::False() -> Bool* {
  return kFalse;
}

auto Bool::True() -> Bool* {
  return kTrue;
}

auto Bool::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Bool(";
  ss << "value=" << GetValue();
  ss << ")";
  return ss.str();
}

void Number::Init() {}

auto Number::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Number(";
  ss << "value=" << GetValue();
  ss << ")";
  return ss.str();
}

void Int::Init() {}

auto Int::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Int(value=" << GetValue() << ")";
  return ss.str();
}

void Float::Init() {}

auto Float::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Float(value=" << GetValue() << ")";
  return ss.str();
}

void String::Init() {}

auto String::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "String(";
  ss << "value=" << GetValue();
  ss << ")";
  return ss.str();
}

auto String::GetHash() const -> String::HashType {
  static constexpr const auto kHashSeed = 0;
  const char* data = GetData().data();
  return static_cast<size_t>(XXH64(data, GetLength(), kHashSeed));
}

void Seq::Init() {}

auto Seq::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Seq(";
  if (!IsEmpty()) {
    ss << "values=[";
    for (auto idx = 0; idx < GetLength(); idx++) {
      const auto value = GetValueAt(idx);
      if (!value)
        continue;
      ss << value->ToString();
      if (idx < (GetLength() - 1))
        ss << ", ";
    }
    ss << "]";
  }
  ss << ")";
  return ss.str();
}

auto Seq::VisitChildren(ObjectVisitor* vis) -> VisitResult {
  for (const auto& value : values_) {
    if (!vis->Visit(value))
      return VisitResult::kStop;
  }
  return VisitResult::kContinue;
}

#define DEFINE_UNIT_OBJECT_IMPL(Name)              \
  void Name::Init() {}                             \
  auto Name::ToString() const -> std::string {     \
    std::stringstream ss{};                        \
    ss << #Name << "(value=" << GetValue() << ")"; \
    return ss.str();                               \
  }
DEFINE_UNIT_OBJECT_IMPL(Px)
DEFINE_UNIT_OBJECT_IMPL(Pct)
DEFINE_UNIT_OBJECT_IMPL(Fr)
#undef DEFINE_UNIT_OBJECT_IMPL

#define DEFINE_BUILTIN_COLOR(Name) Color* Color::k##Name = nullptr;
FOR_EACH_BUILTIN_COLOR(DEFINE_BUILTIN_COLOR)
#undef DEFINE_BUILTIN_COLOR

void Color::Init() {
  Color::kRed = Color::New(255, 0, 0);
  Color::kGreen = Color::New(0, 255, 0);
  Color::kBlue = Color::New(0, 0, 255);
  Color::kBlack = Color::New(0, 0, 0);
  Color::kWhite = Color::New(255, 255, 255);
}

auto Color::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Color()";  // TODO(@s0cks): better printing for colors
  return ss.str();
}

static inline constexpr std::optional<uint8_t> HexCharToInt(char c) noexcept {
  if (c >= '0' && c <= '9')
    return static_cast<uint8_t>(c - '0');
  if (c >= 'a' && c <= 'f')
    return static_cast<uint8_t>(c - 'a' + 10);
  if (c >= 'A' && c <= 'F')
    return static_cast<uint8_t>(c - 'A' + 10);
  return std::nullopt;
}

static inline constexpr Color* ParseHex(const std::string hex) noexcept {
  size_t start_pos = 0;
  if (hex[0] == '#')
    start_pos++;

  if (hex.length() == 6) {
    auto r1 = HexCharToInt(hex[start_pos + 0]);
    auto r2 = HexCharToInt(hex[start_pos + 1]);

    auto g1 = HexCharToInt(hex[start_pos + 2]);
    auto g2 = HexCharToInt(hex[start_pos + 3]);

    auto b1 = HexCharToInt(hex[start_pos + 4]);
    auto b2 = HexCharToInt(hex[start_pos + 5]);

    auto a1 = HexCharToInt(hex[start_pos + 6]);
    auto a2 = HexCharToInt(hex[start_pos + 7]);

    if (!r1 || !r2 || !g1 || !g2 || !b1 || !b2 || !a1 || !a2)
      return nullptr;

    const auto r = static_cast<uint8_t>((*r1 << 4) | *r2);
    const auto g = static_cast<uint8_t>((*g1 << 4) | *g2);
    const auto b = static_cast<uint8_t>((*b1 << 4) | *b2);
    const auto a = static_cast<uint8_t>((*a1 << 4) | *a2);

    return Color::New(r, g, b, a);
  } else if (hex.length() == 3) {
    auto r1 = HexCharToInt(hex[start_pos + 0]);
    auto g1 = HexCharToInt(hex[start_pos + 1]);
    auto b1 = HexCharToInt(hex[start_pos + 2]);

    if (!r1 || !g1 || !b1)
      return nullptr;

    const auto r = static_cast<uint8_t>((*r1 << 4) | *r1);
    const auto g = static_cast<uint8_t>((*g1 << 4) | *g1);
    const auto b = static_cast<uint8_t>((*b1 << 4) | *b1);

    return Color::New(r, g, b, 255);
  }

  return nullptr;
}

static inline constexpr Color* ParseNamedColor(std::string rhs) {
  std::transform(rhs.begin(), rhs.end(), rhs.begin(), [](unsigned char c) {
    return std::tolower(c);
  });

  if (rhs == "red")
    return Color::Red();
  else if (rhs == "green")
    return Color::Green();
  else if (rhs == "blue")
    return Color::Blue();
  else if (rhs == "white")
    return Color::White();
  else if (rhs == "black")
    return Color::Black();

  return nullptr;
}

auto Color::Parse(const std::string rhs) -> Color* {
  if (rhs.empty())
    return nullptr;

  if (rhs.starts_with('#'))
    return ParseHex(std::move(rhs));

  const auto named = ParseNamedColor(rhs);
  if (named)
    return named;

  return ParseHex(std::move(rhs));
}
}  // namespace kura
