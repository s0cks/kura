#ifndef KURA_OBJECT_H
#define KURA_OBJECT_H

#include "common.h"
#include "type.h"

namespace kura {
class Object;
class ObjectVisitor {
 protected:
  ObjectVisitor() = default;

 public:
  virtual ~ObjectVisitor() = default;
  virtual auto Visit(Object* obj) -> VisitResult = 0;
};

class Object {
 protected:
  Object() = default;

 public:
  virtual ~Object() = default;
  virtual auto GetType() const -> TypeId = 0;
  virtual auto ToString() const -> std::string = 0;

  virtual auto VisitChildren(ObjectVisitor* vis) -> VisitResult {
    return VisitResult::kContinue;
  }
};

template <const TypeId Type>
class TemplateObject : public Object {
 protected:
  TemplateObject() = default;

 public:
  ~TemplateObject() override = default;

  auto GetType() const -> TypeId override {
    return Type;
  }
};

template <const TypeId Type, typename T>
class TemplateValueObject : public TemplateObject<Type> {
 private:
  T value_{};

 protected:
  explicit TemplateValueObject(const T value) :
    value_(std::move(value)) {}

 public:
  ~TemplateValueObject() override = default;

  auto GetValue() const -> const T& {
    return value_;
  }
};

#define DECLARE_TYPE(Name)                \
  friend class Type;                      \
                                          \
 private:                                 \
  static void Init();                     \
                                          \
 public:                                  \
  static inline auto GetTyoe() -> Type* { \
    return Type::Name##Type();            \
  }                                       \
                                          \
 public:                                  \
  auto ToString() const -> std::string override;

class None : public TemplateObject<kNoneType> {
 public:
  None() = default;
  ~None() override = default;

  DECLARE_TYPE(None);

 public:
  static auto Get() -> None*;
  static inline auto New() -> None* {
    return new None();
  }
};

class Bool : public TemplateValueObject<kBoolType, bool> {
 public:
  explicit Bool(const bool value) :
    TemplateValueObject(value) {}
  ~Bool() override = default;

  DECLARE_TYPE(Bool);

 public:
  static inline auto New(const bool value) -> Bool* {
    return new Bool(value);
  }

  static auto False() -> Bool*;
  static auto True() -> Bool*;
};

class Number : public TemplateValueObject<kNumberType, double> {
 public:
  explicit Number(const double value) :
    TemplateValueObject(value) {}
  ~Number() override = default;

  DECLARE_TYPE(Number);

 public:
  static inline auto New(const double value) -> Number* {
    return new Number(value);
  }
};

class Int : public TemplateValueObject<kIntType, int64_t> {
 public:
  explicit Int(const int64_t value) :
    TemplateValueObject(value) {}
  ~Int() override = default;

  DECLARE_TYPE(Int);

 public:
  static inline auto New(const int64_t value) -> Int* {
    return new Int(value);
  }
};

class Float : public TemplateValueObject<kFloatType, double> {
 public:
  explicit Float(const double value) :
    TemplateValueObject(value) {}
  ~Float() override = default;

  DECLARE_TYPE(Float);

 public:
  static inline auto New(const double value) -> Float* {
    return new Float(value);
  }
};

class String : public TemplateValueObject<kStringType, std::string> {
 public:
  using HashType = uint64_t;  // TODO(@s0cks): merge with global hash type
 public:
  explicit String(const std::string value) :
    TemplateValueObject(std::move(value)) {}
  ~String() override = default;

  inline auto GetData() const -> std::string_view {
    return GetValue().data();
  }

  inline auto GetLength() const -> uint64_t {
    return GetValue().length();
  }

  auto GetHash() const -> HashType;
  DECLARE_TYPE(String);

 public:
  static inline auto New(const std::string value) -> String* {
    return new String(std::move(value));
  }
};

struct StringHash {
  using is_transparent = void;

  auto operator()(const String& k) const noexcept -> size_t {
    const auto data = k.GetData().data();
    return static_cast<size_t>(XXH64(data, k.GetLength(), 0));
  }
};

class Seq : public TemplateObject<kSeqType> {
 private:
  std::vector<Object*> values_{};

 public:
  explicit Seq(const std::vector<Object*> values = {}) :
    values_(std::move(values)) {}

  auto GetLength() const -> uint64_t {
    return values_.size();
  }

  auto GetValueAt(const uint64_t idx) const -> Object* {
    return values_[idx];
  }

  inline auto HasValueAt(const uint64_t idx) const -> bool {
    return GetValueAt(idx) != nullptr;
  }

  inline auto IsEmpty() const -> bool {
    return values_.empty();
  }

  auto VisitChildren(ObjectVisitor* vis) -> VisitResult override;
  DECLARE_TYPE(Seq);

 public:
  static inline auto New(const std::vector<Object*> values = {}) -> Seq* {
    return new Seq(std::move(values));
  }
};

#define DEFINE_UNIT_OBJECT(Name)                                   \
  class Name : public TemplateValueObject<k##Name##Type, double> { \
   public:                                                         \
    explicit Name(const double value) :                            \
      TemplateValueObject(value) {}                                \
    ~Name() override = default;                                    \
                                                                   \
    DECLARE_TYPE(Name);                                            \
                                                                   \
   public:                                                         \
    static inline auto New(const double value) -> Name* {          \
      return new Name(value);                                      \
    }                                                              \
  };
DEFINE_UNIT_OBJECT(Px);
DEFINE_UNIT_OBJECT(Pct);
DEFINE_UNIT_OBJECT(Fr);
#undef DEFINE_UNIT_OBJECT

class Color : public TemplateObject<kColorType> {
 private:
  uint8_t red_;
  uint8_t blue_;
  uint8_t green_;
  uint8_t alpha_;

 public:
  explicit Color(const uint8_t red, const uint8_t blue, const uint8_t green, const uint8_t alpha) :
    red_(red),
    blue_(blue),
    green_(green),
    alpha_(alpha) {}
  ~Color() override = default;

  auto GetRed() const -> uint8_t {
    return red_;
  }

  void SetRed(const uint8_t rhs) {
    red_ = rhs;
  }

  auto GetBlue() const -> uint8_t {
    return blue_;
  }

  void SetBlue(const uint8_t rhs) {
    blue_ = rhs;
  }

  auto GetGreen() const -> uint8_t {
    return green_;
  }

  void SetGreen(const uint8_t rhs) {
    green_ = rhs;
  }

  auto GetAlpha() const -> uint8_t {
    return alpha_;
  }

  void SetAlpha(const uint8_t rhs) {
    alpha_ = rhs;
  }

  constexpr auto ToRGBA() const -> uint32_t {
    return (static_cast<uint32_t>(red_) << 24) | (static_cast<uint32_t>(green_) << 16) |
           (static_cast<uint32_t>(blue_) << 8) | static_cast<uint32_t>(alpha_);
  }

  constexpr auto ToARGB() const -> uint32_t {
    return (static_cast<uint32_t>(alpha_) << 24) | (static_cast<uint32_t>(red_) << 16) |
           (static_cast<uint32_t>(green_) << 8) | static_cast<uint32_t>(blue_);
  }

  DECLARE_TYPE(Color);

 private:
#define FOR_EACH_BUILTIN_COLOR(V) \
  V(Red)                          \
  V(Blue)                         \
  V(Green)                        \
  V(White)                        \
  V(Black)

#define DECLARE_BUILTIN_COLOR(Name) static Color* k##Name;

  FOR_EACH_BUILTIN_COLOR(DECLARE_BUILTIN_COLOR)
#undef DECLARE_BUILTIN_COLOR

 public:
  static auto Parse(const std::string rhs) -> Color*;

  static inline auto New(const uint8_t red, const uint8_t blue, const uint8_t green, const uint8_t alpha = 255)
      -> Color* {
    return new Color(red, blue, green, alpha);
  }

#define DEFINE_COLOR_GETTER(Name)       \
  static inline auto Name() -> Color* { \
    return k##Name;                     \
  }

  FOR_EACH_BUILTIN_COLOR(DEFINE_COLOR_GETTER)
#undef DEFINE_COLOR_GETTER
};
}  // namespace kura

#endif  // KURA_OBJECT_H
