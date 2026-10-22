#ifndef KURA_RENDERER_VK_H
#define KURA_RENDERER_VK_H

#ifndef KURA_RENDERER_H
#error "Please include <renderer/renderer.h> instead."
#endif  // KURA_RENDERER_H

#ifdef KURA_RENDER_TARGET_VULKAN

namespace kura {
class Renderer {
 public:
  static void Init();
};
}  // namespace kura

#endif  // KURA_RENDER_TARGET_VULKAN

#endif  // KURA_RENDERER_GL_H
