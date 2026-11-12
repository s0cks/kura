#include <iostream>
#include <vector>

#include "backend/runtime_abi.h"

namespace {
struct Region {
  double x = 0;
  double y = 0;
  double width = 0;
  double height = 0;
  uint64_t kind = 0;
  kura::rt::KuraValue message{};

  friend auto operator<<(std::ostream& stream, const Region& rhs) -> std::ostream& {
    stream << "Region{";
    stream << "x=" << rhs.x << ", ";
    stream << "y=" << rhs.y << ", ";
    stream << "width=" << rhs.width << ", ";
    stream << "height=" << rhs.height << ", ";
    stream << "kind=" << rhs.kind;
    stream << "}";
    return stream;
  }
};

// Like the renderer, the places belong to the thread that runs the view.
thread_local std::vector<Region> tl_regions{};
}  // namespace

extern "C" {
void kura_rt_event_add(const double x, const double y, const double width, const double height, const uint64_t kind,
                       const uint64_t tag, const uint64_t bits) {
  tl_regions.push_back({
      x,
      y,
      width,
      height,
      kind,
      {tag, bits},
  });
}

void kura_rt_events_clear() {
  tl_regions.clear();
}

uint64_t kura_rt_event_count() {
  return tl_regions.size();
}

int kura_rt_event_hit(const uint64_t kind, const double x, const double y, kura::rt::KuraValue* message) {
  if (message == nullptr)
    return 0;

  // the last drawn is the one on top
  for (auto it = tl_regions.rbegin(); it != tl_regions.rend(); ++it) {
    if (it->kind != kind)
      continue;
    if (x >= it->x && x < it->x + it->width && y >= it->y && y < it->y + it->height) {
      *message = it->message;
      return 1;
    }
  }
  return 0;
}
}
