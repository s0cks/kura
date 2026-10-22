#ifndef KURA_RENDERER_VERTEX_H
#define KURA_RENDERER_VERTEX_H

#include "glm/glm.hpp"
#include "renderer/color.h"

namespace kura {
struct Vertex {
  glm::vec2 pos;
  Color color;

#ifdef KURA_RENDER_TARGET_OPENGL

  static void InitAttributes();

#endif  // KURA_RENDER_TARGET_OPENGL
};

struct TexVertex {
  glm::vec2 pos;
  glm::vec2 uv;
  Color color;
};

#ifdef KURA_RENDER_TARGET_OPENGL

static constexpr auto CalcVertexBufferSize(const uint64_t num_vertices) -> uint64_t {
  return num_vertices * sizeof(Vertex);
}

static constexpr auto CalcTexVertexBufferSize(const uint64_t num_vertices) -> uint64_t {
  return num_vertices * sizeof(TexVertex);
}

#endif  // KURA_RENDER_TARGET_OPENGL
}  // namespace kura

#endif  // KURA_RENDERER_VERTEX_H
