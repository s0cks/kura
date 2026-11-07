#ifndef KURA_VIEW_H
#define KURA_VIEW_H

#include "function.h"

namespace kura {
using ViewId = uint64_t;

static constexpr const auto kInvalidViewId = 0;
static constexpr const auto kFirstViewId = 1;

class View {
 private:
  ViewId id_;
  Function* init_ = nullptr;

  explicit View(const ViewId id) :
    id_(id) {}

  inline void SetInit(Function* rhs) {
    init_ = rhs;
  }

 public:
  ~View() = default;

  auto GetViewId() const -> ViewId {
    return id_;
  }

  auto GetInit() const -> Function* {
    return init_;
  }

  auto HasInit() const -> bool {
    return GetInit() != nullptr;
  }

  auto Equals(const View& rhs) const -> bool;
  auto ToString() const -> std::string;

 public:
  static auto Get(const ViewId rhs) -> View*;
  static auto New() -> View*;
};
}  // namespace kura

#endif  // KURA_VIEW_H
