#include "view.h"

#include <sstream>
#include <vector>

namespace kura {
namespace {
ViewId current_id_ = kFirstViewId;
std::unordered_map<ViewId, View*> views_{};

// What each thread is running in.
thread_local std::vector<View*> tl_entered{};
thread_local View* tl_base = nullptr;
thread_local std::unique_ptr<View> tl_own{};

inline auto AllocViewId() -> ViewId {
  return current_id_++;
}
}  // namespace

auto View::Equals(const View& rhs) const -> bool {
  return GetViewId() == rhs.GetViewId();
}

auto View::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "View(";
  ss << "id=" << GetViewId();
  ss << ")";
  return ss.str();
}

auto View::New() -> View* {
  const auto view = new View(AllocViewId());
  views_[view->GetViewId()] = view;
  return view;
}

auto View::Get(const ViewId rhs) -> View* {
  const auto it = views_.find(rhs);
  return it == views_.end() ? nullptr : it->second;
}

void View::Destroy(View* view) {
  if (view == nullptr)
    return;
  views_.erase(view->GetViewId());
  if (tl_base == view)
    tl_base = nullptr;
  std::erase(tl_entered, view);
  delete view;
}

auto View::Current() -> View& {
  if (!tl_entered.empty())
    return *tl_entered.back();
  if (tl_base != nullptr)
    return *tl_base;
  if (!tl_own)
    tl_own.reset(new View(AllocViewId()));
  return *tl_own;
}

void View::SetBase(View* view) {
  tl_base = view;
}

auto View::GetBase() -> View* {
  return tl_base;
}

void View::Enter(View* view) {
  tl_entered.push_back(view != nullptr ? view : &Current());
}

void View::Leave() {
  if (!tl_entered.empty())
    tl_entered.pop_back();
}

auto View::Depth() -> size_t {
  return tl_entered.size();
}

void View::Unwind(const size_t depth) {
  while (tl_entered.size() > depth)
    tl_entered.pop_back();
}
}  // namespace kura
