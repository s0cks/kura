#ifndef KURA_LOCAL_SCOPE_H
#define KURA_LOCAL_SCOPE_H

#include <string>
#include <unordered_map>

// clang-format off
#define XXH_INLINE_ALL
#include <xxhash.h>
// clang-format on

namespace kura {
class String;
class LocalScope;
class LocalVariable {
  friend class LocalScope;

 private:
  LocalScope* owner_ = nullptr;
  String* name_;

  inline void SetOwner(LocalScope* rhs) {
    owner_ = rhs;
  }

 public:
  explicit LocalVariable(String* name) :
    name_(name) {}
  ~LocalVariable() = default;

  auto GetOwner() const -> LocalScope* {
    return owner_;
  }

  inline auto HasOwner() const -> bool {
    return owner_;
  }

  auto GetName() const -> String* {
    return name_;
  }

  auto ToString() const -> std::string;

 public:
  static auto New(const std::string name) -> LocalVariable*;
  static inline auto New(String* name) -> LocalVariable* {
    return new LocalVariable(name);
  }
};

class LocalScope {
  struct XXHash64String {
    using is_transparent = void;

    auto operator()(const std::string_view k) const noexcept -> size_t {
      return static_cast<size_t>(XXH64(k.data(), k.size(), 0));
    }
  };

  using LocalMap = std::unordered_map<std::string, LocalVariable*, XXHash64String, std::equal_to<>>;

 private:
  LocalScope* parent_;
  LocalMap locals_{};

  auto Insert(LocalVariable* local) -> bool;

 public:
  explicit LocalScope(LocalScope* parent = nullptr) :
    parent_(parent) {}
  ~LocalScope() = default;

  auto GetParent() const -> LocalScope* {
    return parent_;
  }

  inline auto HasParent() const -> bool {
    return GetParent() != nullptr;
  }

  inline auto IsRoot() const -> bool {
    return !HasParent();
  }

  auto GetLocals() const -> const LocalMap& {
    return locals_;
  }

  inline auto HasLocal(const std::string name) const -> bool {
    return GetLocal(std::move(name)) != nullptr;
  }

  auto CreateLocal(const std::string name) -> LocalVariable*;
  auto CreateLocal(String* name) -> LocalVariable*;
  auto GetLocal(const std::string name) const -> LocalVariable*;
  auto GetLocalRecursive(const std::string name) const -> LocalVariable*;

 public:
  static inline auto New(LocalScope* parent = nullptr) -> LocalScope* {
    return new LocalScope(parent);
  }
};
}  // namespace kura

#endif  // KURA_LOCAL_SCOPE_H
