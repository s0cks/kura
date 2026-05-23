#ifndef KURA_TYPE_H
#define KURA_TYPE_H

#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kura {
#define FOR_EACH_TYPE(V)                                                       \
  V(Object)                                                                    \
  V(Number)                                                                    \
  V(String)                                                                    \
  V(Bool)

struct Value;
#define DECLARE_TYPE(Name) struct Name;
FOR_EACH_TYPE(DECLARE_TYPE)
#undef DECLARE_TYPE
using ValuePtr = std::shared_ptr<Value>;
using ValueList = std::vector<ValuePtr>;

enum ValueType : uint8_t {
  kUnknownType = 0,
#define DEFINE_TYPE(Name) k##Name##Type,
  FOR_EACH_TYPE(DEFINE_TYPE)
#undef DEFINE_TYPE
};

struct Value {
  ValueType type;

  constexpr Value(const ValueType t) : type(t) {}
  constexpr Value(const Value &rhs) = default;
  constexpr Value(Value &&rhs) = default;
  ~Value() = default;

  auto operator=(const Value &rhs) -> Value & = default;
  auto operator=(Value &&rhs) -> Value & = default;
};

class ValueVisitor {
public:
  ValueVisitor() = default;
  virtual ~ValueVisitor() = default;
  virtual auto Visit(ValuePtr &value) -> bool = 0;
};

using PropertyMap = std::unordered_map<std::string, ValuePtr>;

class PropertyKeyVisitor {
public:
  PropertyKeyVisitor() = default;
  virtual ~PropertyKeyVisitor() = default;
  virtual auto Visit(const std::string &key) -> bool = 0;
};

class PropertyVisitor {
public:
  PropertyVisitor() = default;
  virtual ~PropertyVisitor() = default;
  virtual auto VisitProperty(const std::string &name, ValuePtr &value)
      -> bool = 0;
};

struct Object : Value {
  PropertyMap properties{};

  constexpr Object() : Value(kObjectType) {}
  Object(const PropertyMap props)
      : Value(kObjectType), properties(std::move(props)) {}
  constexpr Object(const Object &rhs) = default;
  constexpr Object(Object &&rhs) = default;
  ~Object() = default;

  auto AddProperty(const std::string name, ValuePtr value) -> bool;
  auto GetProperty(const std::string name) const -> ValuePtr;
  auto VisitAllPropertyKeys(PropertyKeyVisitor *vis) -> bool;
  auto VisitAllPropertyValues(ValueVisitor *vis) -> bool;
  auto VisitAllProperties(PropertyVisitor *vis) -> bool;

  auto operator=(const Object &rhs) -> Object &;
  auto operator=(Object &&rhs) -> Object &;
};

struct Bool : Value {
  bool value = false;

  constexpr Bool(const bool val = false) : Value(kBoolType), value(val) {}
  constexpr Bool(const Bool &rhs) = default;
  constexpr Bool(Bool &&rhs) = default;
  ~Bool() = default;

  auto operator=(const Bool &rhs) -> Bool & = default;
  auto operator=(Bool &&rhs) -> Bool & = default;
};

struct Number : Value {
  double value = 0.0;

  constexpr Number(const double val) : Value(kNumberType), value(val) {}
  constexpr Number(const Number &rhs) = default;
  constexpr Number(Number &&rhs) = default;
  ~Number() = default;

  auto operator=(const Number &rhs) -> Number & = default;
  auto operator=(Number &&rhs) -> Number & = default;
};

struct String : Value {
  std::string value{};

  constexpr String() : Value(kStringType) {}
  String(const std::string val) : Value(kStringType), value(val) {}
  constexpr String(const std::string_view val)
      : Value(kStringType), value(val) {}
  constexpr String(const String &rhs) = default;
  constexpr String(String &&rhs) = default;
  ~String() = default;

  auto operator=(const String &rhs) -> String & = default;
  auto operator=(String &&rhs) -> String & = default;
};
} // namespace kura

#endif // KURA_TYPE_H
