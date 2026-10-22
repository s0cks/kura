#ifndef KURA_RENDERER_INDEX_H
#define KURA_RENDERER_INDEX_H

#include <cstdint>

#ifdef KURA_RENDER_TARGET_OPENGL

#include <GL/gl.h>

#endif  // KURA_RENDER_TARGET_OPENGL

namespace kura {
using Index = uint32_t;

#ifdef KURA_RENDER_TARGET_OPENGL

static constexpr const auto kIndexType = GL_UNSIGNED_INT;

static constexpr auto CalcIndexBufferSize(const uint64_t num_indices) -> uint64_t {
  return num_indices * sizeof(Index);
}

#endif  // KURA_RENDER_TARGET_OPENGL
}  // namespace kura

#endif  // KURA_RENDERER_INDEX_H
