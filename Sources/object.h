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
}  // namespace kura

#endif  // KURA_OBJECT_H
