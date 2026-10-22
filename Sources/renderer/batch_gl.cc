#ifdef KURA_RENDER_TARGET_OPENGL

#include "renderer/batch_gl.h"

#include "renderer/index.h"

namespace kura {
Batch::Batch() {
  AllocVertexArrays(&vao_, 1);
  AllocBuffers(buffers_.data(), 2);

  VaoBindScope vao(vao_);
  VboBindScope vbo(GetVbo());
  vbo.BufferData(NULL, kVertexBufferMaxSizeInBytes);

  IboBindScope ibo(GetIbo());
  ibo.BufferData(NULL, kIndexBufferMaxSizeInBytes);

  Vertex::InitAttributes();
}

Batch::~Batch() {
  DeleteVertexArrays(&vao_, 1);
  DeleteBuffers(buffers_.data(), 2);
}

void Batch::Flush() {
  if (IsEmpty())
    return;

  VaoBindScope vao(GetVao());
  VboBindScope vbo(GetVbo());
  vbo.BufferSubData(GetVboData(), GetVboSize());

  IboBindScope ibo(GetIbo());
  ibo.BufferSubData(GetIboData(), GetIboSize());

  glDrawElements(GetMode(), GetNumberOfIndices(), kIndexType, 0);
  num_vertices_ = 0;
  num_indices_ = 0;
}
}  // namespace kura

#endif  // KURA_RENDER_TARGET_OPENGL
