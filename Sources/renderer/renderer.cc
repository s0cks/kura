#include "renderer/renderer.h"

#include "common.h"

namespace kura {
thread_local Renderer* renderer_;

void Renderer::Init() {
  SetRenderer(new Renderer(glm::vec2(1024, 256)));
}

void Renderer::DeInit() {
  delete renderer_;
}

void Renderer::SetRenderer(Renderer* rhs) {
  renderer_ = rhs;
}

auto Renderer::Get() -> Renderer* {
  return renderer_;
}
}  // namespace kura
