#include "type.h"

#include <sstream>

#include "object.h"
#include "property.h"
#include "record.h"
#include "type_id.h"

namespace kura {
#define DEFINE_BUILTIN_TYPE(Name)               \
  static Type* k##Name##TypeInstance = nullptr; \
  auto Type::Name##Type()->Type* {              \
    return k##Name##TypeInstance;               \
  }
FOR_EACH_TYPE(DEFINE_BUILTIN_TYPE)
#undef DEFINE_BUILTIN_TYPE

struct XXStringHash {
  using is_transparent = void;

  auto operator()(const String* k) const noexcept -> size_t {
    const auto& data = k->GetData();
    return static_cast<size_t>(XXH64(data.data(), data.size(), 0));
  }
};

struct StringEqual {
  auto operator()(const String* lhs, const String* rhs) const noexcept -> bool {
    return lhs->GetData() == rhs->GetData();
  }
};

static std::vector<Type*> types_{};
static TypeId current_id_ = 0;

static inline auto AllocTypeId() -> TypeId {
  return current_id_++;
}

using TypeIdMap = std::unordered_map<String*, TypeId, XXStringHash, StringEqual>;

static TypeIdMap str2typeid_{};

static inline void Register(Type* rhs) {
  types_.push_back(rhs);
}

void Type::Init() {
  // clang-format off
#define DEFINE_TYPE_INIT(Name) \
  k##Name##TypeInstance = New(String::New(#Name));
  // clang-format on
  FOR_EACH_TYPE(DEFINE_TYPE_INIT)
#undef DECLARE_TYPE_INIT

  // clang-format off
#define CALL_INIT(Name) \
    Name::Init();
  // clang-format on
  FOR_EACH_TYPE(CALL_INIT)
#undef CALL_INIT
}

auto Type::Get(const TypeId id) -> Type* {
  return types_.at(static_cast<uint64_t>(id));
}

auto Type::VisitTypes(TypeVisitor* vis) -> VisitResult {
  for (const auto& type : types_) {
    if (!vis->Visit(type))
      return VisitResult::kStop;
  }
  return VisitResult::kContinue;
}

auto Type::New(String* name) -> Type* {
  const auto id = current_id_++;
  const auto type = new Type(id, name);
  Register(type);
  return type;
}

auto Type::NewSum(String* name, const std::vector<Type*> variants) -> Type* {
  const auto id = AllocTypeId();
  const auto new_type = SumType::New(id, name, std::move(variants));
  Register(new_type);
  return new_type;
}

auto Type::NewProduct(String* name, const std::vector<Type*> variants) -> Type* {
  const auto id = AllocTypeId();
  const auto new_type = ProductType::New(id, name, std::move(variants));
  Register(new_type);
  return new_type;
}

auto Type::NewSeq(String* name, Type* elem_type) -> Type* {
  const auto id = AllocTypeId();
  const auto new_type = SeqType::New(id, name, elem_type);
  Register(new_type);
  return new_type;
}

auto Type::FindTypeId(String* rhs) -> TypeId {
  const auto pos = str2typeid_.find(rhs);
  return pos != std::end(str2typeid_) ? pos->second : kInvalidType;
}

auto Type::Equals(const Type& rhs) const -> bool {
  return GetTypeId() == rhs.GetTypeId();
}

auto Type::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Type(";
  ss << "id=" << static_cast<uint64_t>(GetTypeId()) << ", ";
  const auto name = GetTypeName();
  if (name)
    ss << "name=" << name->ToString();
  ss << ")";
  return ss.str();
}

auto SeqType::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "SeqType(";
  ss << "id=" << GetTypeId() << ", ";
  ss << "name=" << GetTypeName()->ToString() << ", ";
  ss << "element_type=" << GetElementType()->ToString();
  ss << ")";
  return ss.str();
}

auto SeqType::Equals(const Type& rhs) const -> bool {
  if (!rhs.IsSeq())
    return false;
  return GetElementType()->Equals(*(reinterpret_cast<const SeqType&>(rhs).GetElementType()));
}

// SplitMix64 finalizer step. Completely breaks linearity of sequential
// numbers.
static inline uint64_t ScrambleSequentialInt(uint64_t val) {
  val = (val ^ (val >> 30)) * 0xbf58476d1ce4e5b9ULL;
  val = (val ^ (val >> 27)) * 0x94d049bb133111ebULL;
  return val ^ (val >> 31);
}

// Multiplier-driven avalanche mixer with golden ratio bias for asymmetry
// protection.
static inline uint64_t MixPair(uint64_t left, uint64_t right) {
  uint64_t hash = left ^ (right + 0x9e3779b97f4a7c15ULL);
  hash = (hash ^ (hash >> 30)) * 0xbf58476d1ce4e5b9ULL;
  hash = (hash ^ (hash >> 27)) * 0x94d049bb133111ebULL;
  return hash ^ (hash >> 31);
}

auto AlgebraicType::ComputeFingerprint(const std::vector<Type*>& types) -> Fingerprint {
  if (types_.empty())
    return kInvalidFingerprint;
  std::vector<TypeId> type_ids{};
  type_ids.reserve(types.size());
  for (const auto& type : types)
    type_ids.push_back(ScrambleSequentialInt(type->GetTypeId()));

  if (type_ids.size() == 1)
    return type_ids[0];

  size_t current_level_size = type_ids.size();
  while (current_level_size > 1) {
    size_t next_level_index = 0;

    for (size_t i = 0; i < current_level_size; i += 2) {
      if (i + 1 < current_level_size) {
        type_ids[next_level_index] = MixPair(type_ids[i], type_ids[i + 1]);
      } else {
        static constexpr uint64_t kOddPadding = 0xABCDEF1234567890ULL;
        type_ids[next_level_index] = MixPair(type_ids[i], kOddPadding);
      }

      ++next_level_index;
    }

    current_level_size = next_level_index;
  }

  return type_ids[0];
}

auto ProductType::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "ProductType(";
  ss << "id=" << GetTypeId() << ", ";
  ss << "name=" << GetTypeName()->ToString() << ", ";
  ss << ")";
  return ss.str();
}

auto ProductType::Equals(const Type& rhs) const -> bool {
  NOT_IMPLEMENTED;  // TODO(@s0cks): implement
  return false;
}

auto SumType::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "SumType(";
  ss << "id=" << GetTypeId() << ", ";
  ss << "name=" << GetTypeName()->ToString() << ", ";
  ss << ")";
  return ss.str();
}

auto SumType::Equals(const Type& rhs) const -> bool {
  NOT_IMPLEMENTED;  // TODO(@s0cks): implement
  return false;
}
}  // namespace kura
