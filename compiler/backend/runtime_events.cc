#include <algorithm>
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
// What is drawn inside a clip can only be hit inside it: a row of a scrolled list that is out of sight isn't there.
thread_local std::vector<Region> tl_clips{};
}  // namespace

extern "C" {
void kura_rt_event_add(const double x, const double y, const double width, const double height, const uint64_t kind,
                       const uint64_t tag, const uint64_t bits) {
  Region region{x, y, width, height, kind, {tag, bits}};
  if (!tl_clips.empty()) {
    const auto& clip = tl_clips.back();
    const auto left = std::max(region.x, clip.x);
    const auto top = std::max(region.y, clip.y);
    const auto right = std::min(region.x + region.width, clip.x + clip.width);
    const auto bottom = std::min(region.y + region.height, clip.y + clip.height);
    region.x = left;
    region.y = top;
    region.width = std::max(0.0, right - left);
    region.height = std::max(0.0, bottom - top);
  }
  tl_regions.push_back(region);
}

void kura_rt_event_clip_push(const double x, const double y, const double width, const double height) {
  Region clip{x, y, width, height, 0, {}};
  if (!tl_clips.empty()) {
    const auto& outer = tl_clips.back();
    const auto left = std::max(clip.x, outer.x);
    const auto top = std::max(clip.y, outer.y);
    const auto right = std::min(clip.x + clip.width, outer.x + outer.width);
    const auto bottom = std::min(clip.y + clip.height, outer.y + outer.height);
    clip = Region{left, top, std::max(0.0, right - left), std::max(0.0, bottom - top), 0, {}};
  }
  tl_clips.push_back(clip);
}

void kura_rt_event_clip_pop() {
  if (!tl_clips.empty())
    tl_clips.pop_back();
}

void kura_rt_events_clear() {
  tl_regions.clear();
  tl_clips.clear();
  kura_rt_scrollers_clear();
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
