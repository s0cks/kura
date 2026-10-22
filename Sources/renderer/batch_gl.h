#ifndef KURA_RENDERER_BATCH_H
#define KURA_RENDERER_BATCH_H

#ifdef KURA_RENDER_TARGET_OPENGL

#include <array>
#include <cstdint>
#include <cstring>
#include <epoxy/gl.h>

#include "renderer/index.h"
#include "renderer/vertex.h"

namespace kura {
class VaoBindScope {
  using Handle = GLuint;

 public:
  explicit VaoBindScope(const Handle handle) {
    glBindVertexArray(handle);
  }
  ~VaoBindScope() {
    glBindVertexArray(0);
  }
};

template <const GLenum Target>
class TemplateBufferBindScope {
 public:
  explicit TemplateBufferBindScope(const GLuint id) {
    glBindBuffer(Target, id);
  }
  ~TemplateBufferBindScope() {
    glBindBuffer(Target, 0);
  }

  template <const GLenum Mode = GL_DYNAMIC_DRAW>
  void BufferData(const uint8_t* bytes, const uint64_t num_bytes) {
    glBufferData(Target, num_bytes, bytes, Mode);
  }

  void BufferSubData(const uint8_t* bytes, const uint64_t num_bytes, const uint64_t offset = 0) {
    glBufferSubData(Target, offset, num_bytes, bytes);
  }
};

using VboBindScope = TemplateBufferBindScope<GL_ARRAY_BUFFER>;
using IboBindScope = TemplateBufferBindScope<GL_ELEMENT_ARRAY_BUFFER>;

static inline void AllocVertexArrays(GLuint* vaos, const uint64_t num_vaos) {
  glGenVertexArrays(num_vaos, vaos);
}

static inline void AllocBuffers(GLuint* buffers, const uint64_t num_buffers) {
  glGenBuffers(num_buffers, buffers);
}

static inline void DeleteVertexArrays(GLuint* vaos, const uint64_t num_vaos) {
  glDeleteVertexArrays(num_vaos, vaos);
}

static inline void DeleteBuffers(GLuint* buffers, const uint64_t num_buffers) {
  glDeleteBuffers(num_buffers, buffers);
}

enum BatchMode : GLenum {
  kLine = GL_LINE,
  kTriangle = GL_TRIANGLES,
};

class Batch {
 public:
  static constexpr const uint64_t kMaxNumberOfVertices = KURA_MAX_NUMBER_OF_VERTICES;
  static constexpr const uint64_t kVertexBufferMaxSizeInBytes = CalcVertexBufferSize(kMaxNumberOfVertices);

  static constexpr const uint64_t kMaxNumberOfIndices = KURA_MAX_NUMBER_OF_INDICES;
  static constexpr const uint64_t kIndexBufferMaxSizeInBytes = CalcIndexBufferSize(kMaxNumberOfIndices);

  using VertexArray = std::array<Vertex, kMaxNumberOfVertices>;
  using IndexArray = std::array<Index, kMaxNumberOfIndices>;

 private:
  static constexpr const auto kVertexBufferPos = 0;
  static constexpr const auto kIndexBufferPos = 1;

 private:
  GLuint vao_ = GL_INVALID_VALUE;
  std::array<GLuint, 2> buffers_{};

  VertexArray vertices_;
  uint64_t num_vertices_;

  IndexArray indices_;
  uint64_t num_indices_;

  inline void SetVertexBuffer(const GLuint rhs) {
    buffers_.at(kVertexBufferPos) = rhs;
  }

  inline void SetVertexAt(const uint64_t idx, const Vertex rhs) {
    vertices_.at(idx) = std::move(rhs);
  }

  inline void SetIndexBuffer(const GLuint rhs) {
    buffers_.at(kIndexBufferPos) = rhs;
  }

  inline void SetIndexAt(const uint64_t idx, const Index rhs) {
    indices_.at(idx) = std::move(rhs);
  }

 public:
  Batch();
  virtual ~Batch();
  virtual auto GetMode() const -> BatchMode = 0;

  auto GetVao() const -> GLuint {
    return vao_;
  }

  auto GetVbo() const -> GLuint {
    return buffers_.at(kVertexBufferPos);
  }

  auto GetVboData() const -> const uint8_t* {
    return reinterpret_cast<const uint8_t*>(vertices_.data());
  }

  auto GetVboSize() const -> uint64_t {
    return CalcVertexBufferSize(num_vertices_);
  }

  auto GetVertices() const -> const VertexArray& {
    return vertices_;
  }

  auto GetNumberOfVertices() const -> uint64_t {
    return num_vertices_;
  }

  auto GetVertexAt(const uint64_t idx) const -> const Vertex& {
    return vertices_.at(idx);
  }

  auto GetIbo() const -> GLuint {
    return buffers_.at(kIndexBufferPos);
  }

  auto GetIboData() const -> const uint8_t* {
    return reinterpret_cast<const uint8_t*>(indices_.data());
  }

  auto GetIboSize() const -> uint64_t {
    return CalcIndexBufferSize(num_indices_);
  }

  auto GetIndices() const -> const IndexArray& {
    return indices_;
  }

  auto GetNumberOfIndices() const -> uint64_t {
    return num_indices_;
  }

  auto GetIndexAt(const uint64_t idx) const -> const Index& {
    return indices_.at(idx);
  }

  auto IsEmpty() const -> bool {
    return vertices_.empty() || indices_.empty();
  }

  void PushVertices(const Vertex* vertices, const uint64_t num_vertices) {
    if ((num_vertices_ + num_vertices) >= kMaxNumberOfVertices)
      return;
    memcpy(&vertices_[num_vertices_], vertices, sizeof(Vertex) * num_vertices);
    num_vertices_ += num_vertices;
  }

  inline void PushVertices(const std::vector<Vertex>& rhs) {
    return PushVertices(rhs.data(), rhs.size());
  }

  template <const uint64_t NumberOfVertices>
  inline void PushVertices(const std::array<Vertex, NumberOfVertices>& rhs) {
    return PushVertices(rhs.data(), rhs.size());
  }

  void PushIndices(const Index* indices, const uint64_t num_indices) {
    if ((num_indices_ + num_indices) >= kMaxNumberOfIndices)
      return;
    memcpy(&indices_[num_indices_], indices, sizeof(Vertex) * num_indices);
    num_indices_ += num_indices;
  }

  inline void PushIndices(const std::vector<Index>& rhs) {
    return PushIndices(rhs.data(), rhs.size());
  }

  template <const uint64_t NumberOfIndices>
  inline void PushIndices(const std::array<Index, NumberOfIndices>& rhs) {
    return PushIndices(rhs.data(), rhs.size());
  }

  inline void Push(const Vertex* vertices, const uint64_t num_vertices, const Index* indices,
                   const uint64_t num_indices) {
    PushVertices(vertices, num_vertices);
    PushIndices(indices, num_indices);
  }

  template <const uint64_t NumberOfVertices, const uint64_t NumberOfIndices>
  inline void Push(const std::array<Vertex, NumberOfVertices>& vertices,
                   const std::array<Index, NumberOfIndices>& indices) {
    PushVertices<NumberOfVertices>(vertices);
    PushIndices<NumberOfIndices>(indices);
  }

  void Flush();
};

template <const BatchMode Mode>
class TemplateBatch : public Batch {
 public:
  TemplateBatch() = default;
  ~TemplateBatch() override = default;

  auto GetMode() const -> BatchMode override {
    return Mode;
  }
};

using LineBatch = TemplateBatch<kLine>;
using TriangleBatch = TemplateBatch<kTriangle>;
}  // namespace kura

#endif  // KURA_RENDER_TARGET_OPENGL

#endif  // KURA_RENDERER_BATCH_H
