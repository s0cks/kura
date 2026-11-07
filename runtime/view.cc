#include "view.h"

#include <sstream>

namespace kura {
static ViewId current_id_ = kFirstViewId;
static std::vector<View*> views_{};

static inline auto AllocViewId() -> ViewId {
  return current_id_++;
}

auto View::Equals(const View& rhs) const -> bool {
  return GetViewId() == rhs.GetViewId();
}

auto View::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "View(";
  ss << ")";
  return ss.str();
}

auto View::New() -> View* {
  const auto new_id = AllocViewId();
  return new View(new_id);
}

auto View::Get(const ViewId rhs) -> View* {
  if (rhs >= current_id_)
    return nullptr;
  return views_.at(rhs);
}
}  // namespace kura
