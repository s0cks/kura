#ifndef KURA_TYPE_H
#define KURA_TYPE_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "common.h"
#include "printer.h"
#include "type_id.h"

namespace kura {
class Type;
using TypeVisitor = TemplateVisitor<Type>;
using TypePointerVisitor = TemplatePointerVisitor<Type>;

class String;
class Type {
 private:
  TypeId id_;
  String* name_;

 protected:
  Type(const TypeId id, String* name) :
    id_(id),
    name_(name) {}

 public:
  virtual ~Type() = default;

  auto GetTypeId() const -> TypeId {
    return id_;
  }

  inline auto IsInvalid() const -> bool {
    return GetTypeId() == kInvalidType;
  }

  inline auto IsValid() const -> bool {
    return GetTypeId() != kInvalidType;
  }

  auto GetTypeName() const -> String* {
    return name_;
  }

  virtual auto IsAlgebraic() const -> bool {
    return false;
  }

  virtual auto IsSum() const -> bool {
    return false;
  }

  virtual auto IsProduct() const -> bool {
    return false;
  }

  virtual auto IsSeq() const -> bool {
    return false;
  }

  virtual auto Equals(const Type& rhs) const -> bool;
  virtual auto ToString() const -> std::string;

 private:
  static auto New() -> Type*;

 public:
  static void Init();
  static auto Get(const TypeId id) -> Type*;
  static auto FindTypeId(String* name) -> TypeId;
  static auto New(String* name) -> Type*;
  static auto NewSum(String* name, const std::vector<Type*> variants) -> Type*;
  static auto NewProduct(String* name, const std::vector<Type*> variants) -> Type*;
  static auto NewSeq(String* name, Type* elem_type) -> Type*;
  static auto VisitTypes(TypeVisitor* vis) -> VisitResult;

  static inline auto Find(String* name) -> Type* {
    const auto id = FindTypeId(name);
    if (id == kInvalidType)
      return nullptr;
    return Get(id);
  }

  static inline auto FindOrCreate(String* name) -> Type* {
    const auto type = Find(name);
    if (type)
      return type;
    return New(name);
  }

  // clang-format off
#define DECLARE_BUILTIN_TYPE(Name) \
  static auto Name##Type() -> Type*;
  // clang-format on

  FOR_EACH_TYPE(DECLARE_BUILTIN_TYPE);
#undef DECLARE_BUILTIN_TYPE
};

#define DECLARE_TYPE(Name)                             \
  friend class Type;                                   \
                                                       \
 public:                                               \
  auto ToString() const -> std::string override;       \
  auto Equals(const Type& rhs) const -> bool override; \
  auto Is##Name() const -> bool override {             \
    return true;                                       \
  }

class SeqType : public Type {
 private:
  Type* element_type_;

  explicit SeqType(const TypeId id, String* name, Type* element_type) :
    Type(id, name),
    element_type_(element_type) {}

 public:
  ~SeqType() = default;

  auto GetElementType() const -> Type* {
    return element_type_;
  }

  DECLARE_TYPE(Seq);

 private:
  static inline auto New(const TypeId id, String* name, Type* elem_type) -> Type* {
    return new SeqType(id, name, elem_type);
  }
};

class AlgebraicType : public Type {
 public:
  using Fingerprint = uint64_t;
  static constexpr const auto kInvalidFingerprint = kInvalidType;

 private:
  std::vector<Type*> variants_;
  Fingerprint fingerprint_ = kInvalidFingerprint;

  static auto ComputeFingerprint(const std::vector<Type*>& variants) -> Fingerprint;

 protected:
  explicit AlgebraicType(const TypeId id, String* name, const std::vector<Type*> variants) :
    Type(id, name),
    variants_(std::move(variants)) {
    SetFingerprint(ComputeFingerprint(variants_));
  }

  void SetFingerprint(const Fingerprint rhs) {
    fingerprint_ = rhs;
  }

 public:
  ~AlgebraicType() override = default;

  auto GetFingerprint() const -> Fingerprint {
    return fingerprint_;
  }

  inline auto HasFingerprint() const -> bool {
    return GetFingerprint() != kInvalidFingerprint;
  }

  auto IsAlgebraic() const -> bool override {
    return true;
  }
};

#define DECLARE_ALGEBRAIC_TYPE(Name)                                                                \
  DECLARE_TYPE(Name);                                                                               \
                                                                                                    \
 private:                                                                                           \
  static auto New(const TypeId id, String* name, const std::vector<Type*> variants = {}) -> Type* { \
    return new Name##Type(id, name, std::move(variants));                                           \
  }

class ProductType : public AlgebraicType {
 private:
  ProductType(const TypeId id, String* name, const std::vector<Type*> variants) :
    AlgebraicType(id, name, std::move(variants)) {}

  DECLARE_ALGEBRAIC_TYPE(Product);
};

class SumType : public AlgebraicType {
 private:
  SumType(const TypeId id, String* name, const std::vector<Type*> variants) :
    AlgebraicType(id, name, std::move(variants)) {}

 public:
  ~SumType() override = default;

  DECLARE_ALGEBRAIC_TYPE(Sum);
};

template <typename Stream = std::ostream>
class TypePrinter : public ToStringPrinter<Type, Stream> {
 public:
  explicit TypePrinter(Stream& stream, const Indent indent = {}) :
    ToStringPrinter<Type, Stream>(stream, std::move(indent)) {}
  ~TypePrinter() = default;
};
}  // namespace kura

#undef DECLARE_TYPE

#endif  // KURA_TYPE_H
