#ifdef KURA_RENDER_TARGET_OPENGL

#include <epoxy/gl.h>

#include "renderer/vertex.h"

namespace kura {
void Vertex::InitAttributes() {
  // pos
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));

  // color
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));
}
}  // namespace kura

#endif  // KURA_RENDER_TARGET_OPENGL
