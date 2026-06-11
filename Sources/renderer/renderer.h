#ifndef KURA_RENDERER_H
#define KURA_RENDERER_H

namespace kura {
class Renderer {
 private:
  Renderer() = default;

 public:
  ~Renderer() = default;

 public:
  static void Init();
};
}  // namespace kura

#endif  // KURA_RENDERER_H
