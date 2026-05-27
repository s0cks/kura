#ifndef KURA_TYPE_H
#define KURA_TYPE_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "common.h"

namespace kura {
namespace expr {
class Expr;
class ExprBuilder;
}  // namespace expr

class Type;
using TypeList = std::vector<Type*>;

typedef uint64_t TypeId;

#define FOR_EACH_PRIMITIVE_TYPE(V) \
  V(Bool)                          \
  V(Number)                        \
  V(String)                        \
  V(Seq)                           \
  V(Record)                        \
  V(Union)                         \
  V(Module)                        \
  V(Function)

enum BuiltinTypes : TypeId {
  kInvalidType = 0,
#define DEFINE_TYPE(Name) k##Name##Type,
  FOR_EACH_PRIMITIVE_TYPE(DEFINE_TYPE)
#undef DEFINE_TYPE
};

class Type {
 private:
  TypeId id_;

 public:
  explicit Type(const TypeId id) :
    id_(id) {}
  virtual ~Type() = default;

  auto GetTypeId() const -> TypeId {
    return id_;
  }

  constexpr auto IsBuiltinType() const -> bool {
    // clang-format off
    switch (id_) {
#define DEFINE_CHECK(Name) \
      case k##Name##Type:  \
        return true;
      FOR_EACH_PRIMITIVE_TYPE(DEFINE_CHECK)
      default:
        return false;
    }
    // clang-format on
  }

#define DEFINE_TYPE_CHECK(Name)             \
  constexpr auto Is##Name() const -> bool { \
    return id_ == k##Name##Type;            \
  }
  FOR_EACH_PRIMITIVE_TYPE(DEFINE_TYPE_CHECK)
#undef DEFINE_TYPE_CHECK

  virtual auto GetTypeName() const -> std::string_view = 0;
  virtual auto ToString() const -> std::string = 0;

  virtual auto Equals(const Type& rhs) const -> bool {
    return GetTypeId() == rhs.GetTypeId();
  }
};

template <const TypeId Id>
class TypeTemplate : public Type {
 protected:
  TypeTemplate() :
    Type(Id) {}

 public:
  ~TypeTemplate() override = default;
};

#define DECLARE_TYPE(Name)                                \
 public:                                                  \
  auto GetTypeName() const -> std::string_view override { \
    return #Name;                                         \
  }                                                       \
  auto ToString() const -> std::string override;

class Bool : public TypeTemplate<kBoolType> {
 private:
  bool value_;

 public:
  explicit Bool(const bool value) :
    TypeTemplate<kBoolType>(),
    value_(value) {}
  ~Bool() override = default;

  auto GetValue() const -> bool {
    return value_;
  }

  DECLARE_TYPE(Bool);

 public:
  static inline auto New(const bool value) -> Bool* {
    return new Bool(value);
  }

  static auto True() -> Bool*;
  static auto False() -> Bool*;
};

class String : public TypeTemplate<kStringType> {
 private:
  std::string value_;

 public:
  explicit String(const std::string value) :
    TypeTemplate<kStringType>(),
    value_(std::move(value)) {}
  ~String() override = default;

  auto GetValue() const -> const std::string& {
    return value_;
  }

  DECLARE_TYPE(String);

 public:
  static inline auto New(const std::string value) -> String* {
    return new String(std::move(value));
  }
};

class Number : public TypeTemplate<kNumberType> {
 private:
  double value_;

 public:
  explicit Number(double value) :
    TypeTemplate<kNumberType>(),
    value_(value) {}
  ~Number() override = default;

  auto GetValue() const -> double {
    return value_;
  }

  DECLARE_TYPE(Number);

 public:
  static inline auto New(const double value) -> Number* {
    return new Number(value);
  }
};

class Seq : public TypeTemplate<kSeqType> {
 public:
  Seq() = default;
  ~Seq() override = default;

  DECLARE_TYPE(Seq);

 public:
  static inline auto New() -> Seq* {
    return new Seq();
  }
};

class UnionType : public TypeTemplate<kUnionType> {
 private:
  std::string name_;
  TypeList variants_{};

 public:
  UnionType(const std::string name, const TypeList variants) :
    TypeTemplate<kUnionType>(),
    name_(std::move(name)),
    variants_(std::move(variants)) {}
  ~UnionType() override = default;

  auto GetName() const -> const std::string& {
    return name_;
  }

  auto GetVariants() const -> const TypeList& {
    return variants_;
  }

  DECLARE_TYPE(Union);

 public:
  static inline auto New(const std::string name, const TypeList variants = {}) -> UnionType* {
    return new UnionType(std::move(name), std::move(variants));
  }
};

class Record : public TypeTemplate<kRecordType> {
 public:
  struct Property {
    std::string name;
    Type* type;
  };

  class PropertyVisitor {
   protected:
    PropertyVisitor() = default;

   public:
    virtual ~PropertyVisitor() = default;
    virtual auto VisitProperty(Property* property) -> bool = 0;
  };

  using PropertyMap = std::unordered_map<std::string, Property*>;

 private:
  PropertyMap properties_{};

  auto AddProperty(Property* prop) -> bool {
    const auto pos = properties_.insert({prop->name, prop});
    return pos.second;
  }

 public:
  explicit Record(const PropertyMap properties) :
    TypeTemplate<kRecordType>(),
    properties_(std::move(properties)) {}
  ~Record() override = default;

  auto GetProperties() const -> const PropertyMap& {
    return properties_;
  }

  auto GetProperty(const std::string name) const -> Property* {
    const auto pos = properties_.find(name);
    return pos->second;
  }

  auto VisitProperties(const std::function<bool(Property*)> vis) const -> bool {
    for (const auto& prop : properties_) {
      if (!vis(prop.second))
        return false;
    }
    return true;
  }

  auto VisitProperties(PropertyVisitor* vis) const -> bool {
    for (const auto& prop : properties_) {
      if (!vis->VisitProperty(prop.second))
        return false;
    }
    return true;
  }

  DECLARE_TYPE(Record);

 public:
  static inline auto New(const PropertyMap properties) -> Record* {
    return new Record(std::move(properties));
  }
};

class Function : public TypeTemplate<kFunctionType> {
 private:
  std::string name_;

 public:
  explicit Function(const std::string name) :
    TypeTemplate<kFunctionType>(),
    name_(name) {}
  ~Function() override = default;

  auto GetName() const -> const std::string& {
    return name_;
  }

  DECLARE_TYPE(Function);

 public:
  static inline auto New(const std::string name) -> Function* {
    return new Function(std::move(name));
  }
};

class FunctionVisitor {
 protected:
  FunctionVisitor() = default;

 public:
  virtual ~FunctionVisitor() = default;
  virtual auto VisitFunction(Function* func) -> bool = 0;
};

class Module : public TypeTemplate<kModuleType> {
  friend class expr::ExprBuilder;

 public:
  using FunctionMap = std::unordered_map<std::string, Function*>;

 private:
  std::string name_;
  FunctionMap functions_{};

  auto AddFunction(Function* rhs) -> bool {
    const auto pos = functions_.insert({rhs->GetName(), rhs});
    return pos.second;
  }

 public:
  explicit Module(const std::string name) :
    TypeTemplate<kModuleType>(),
    name_(std::move(name)) {}
  ~Module() override = default;

  auto GetName() const -> const std::string& {
    return name_;
  }

  auto GetFunctions() const -> const FunctionMap& {
    return functions_;
  }

  auto GetFunction(const std::string name) const -> Function* {
    const auto pos = functions_.find(name);
    return pos->second;
  }

  auto VisitFunctions(const std::function<bool(Function*)> vis) -> bool {
    for (const auto& func : functions_) {
      if (!vis(func.second))
        return false;
    }
    return true;
  }

  auto VisitFunctions(FunctionVisitor* vis) -> bool {
    for (const auto& func : functions_) {
      if (!vis->VisitFunction(func.second))
        return false;
    }
    return true;
  }

  DECLARE_TYPE(Name);

 public:
  static inline auto New(const std::string name) -> Module* {
    return new Module(std::move(name));
  }
};
}  // namespace kura

#endif  // KURA_TYPE_H
