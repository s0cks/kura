#ifndef KURA_TYPE_H
#define KURA_TYPE_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "common.h"
#include "type_id.h"

namespace kura {
class String;
class Type {
 private:
  TypeId id_;
  String* name_;

  explicit Type(const TypeId id, String* name) :
    id_(id),
    name_(name) {}

 public:
  ~Type() = default;

  auto GetId() const -> TypeId {
    return id_;
  }

  auto GetName() const -> String* {
    return name_;
  }

  inline auto IsRuntimeType() const -> bool {
    return kura::IsRuntimeType(GetId());
  }

  inline auto IsBuiltinType() const -> bool {
    return kura::IsBuiltinType(GetId());
  }

  auto Equals(const Type& rhs) const -> bool;
  auto ToString() const -> std::string;

  friend auto operator==(const Type& lhs, const Type& rhs) -> bool {
    return lhs.Equals(rhs);
  }

  friend auto operator!=(const Type& lhs, const Type& rhs) -> bool {
    return !lhs.Equals(rhs);
  }

  friend auto operator<<(std::ostream& stream, const Type& rhs) -> std::ostream& {
    return stream << rhs.ToString();
  }

 private:
  static auto AllocTypeId() -> TypeId;

 public:
  static void Init();
  static auto Get(const TypeId id) -> Type*;
  static auto Find(const std::string name) -> Type*;
  static auto Find(String* name) -> Type*;
  static auto FindOrCreate(String* name) -> Type*;
  static auto FindOrCreate(const std::string name) -> Type*;
  static auto New(String* name) -> Type*;
  static auto New(const std::string name) -> Type*;
};
}  // namespace kura

#endif  // KURA_TYPE_H
