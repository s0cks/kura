#ifdef KURA_RENDER_TARGET_OPENGL

#include "common.h"
#include "renderer/renderer.h"

namespace kura {
RenderContext::RenderContext(Renderer* owner) :
  owner_(owner) {
  tris_ = std::make_unique<TriangleBatch>();
  lines_ = std::make_unique<LineBatch>();
}

Shader::Shader(const Kind kind, const std::string_view source) {
  handle_ = glCreateShader(kind);
  std::array<const char*, 1> data = {
      source.data(),
  };

  glShaderSource(GetHandle(), data.size(), data.data(), NULL);
  glCompileShader(GetHandle());
}

Shader::~Shader() {
  glDeleteShader(handle_);
}

Renderer::Renderer(const glm::vec2 resolution) :
  resolution_(std::move(resolution)) {
  const auto fragment_source =
      "#version 330 core\n"
      "in  vec4 v_color;\n"
      "out vec4 out_color;\n"
      "void main() { out_color = v_color; }\n";
  Shader fs(Shader::kFragment, fragment_source);

  const auto vertex_source =
      "#version 330 core\n"
      "layout(location = 0) in vec2 a_pos;\n"
      "layout(location = 1) in vec4 a_color;\n"
      "uniform vec2 u_res;\n"
      "out vec4 v_color;\n"
      "void main() {\n"
      "  vec2 p = a_pos / u_res * 2.0 - 1.0;\n"
      "  gl_Position = vec4(p.x, -p.y, 0.0, 1.0);\n"
      "  v_color = a_color;\n"
      "}\n";
  Shader vs(Shader::kVertex, vertex_source);

  program_ = glCreateProgram();
  glAttachShader(GetProgram(), vs);
  glAttachShader(GetProgram(), fs);
  glLinkProgram(GetProgram());

  loc_res_ = glGetUniformLocation(GetProgram(), "u_res");
  ctx_ = std::make_unique<RenderContext>(this);
}

Renderer::~Renderer() {
  glDeleteProgram(GetProgram());
}

void RenderContext::Flush() {
  glUseProgram(GetOwner()->GetProgram());
  GetTriangles()->Flush();
  GetLines()->Flush();
}

template <const uint64_t NumberOfVertices>
using TemplateVertexArray = std::array<Vertex, NumberOfVertices>;

template <const uint64_t NumberOfIndices>
using TemplateIndexArray = std::array<Index, NumberOfIndices>;

void RenderContext::DrawRect(const glm::vec2 pos, const glm::vec2 size) {
  static constexpr const auto kTotalNumberOfVertices = 4;
  using VertexArray = TemplateVertexArray<kTotalNumberOfVertices>;

  static constexpr const auto kTotalNumberOfIndices = 8;
  using IndexArray = TemplateIndexArray<kTotalNumberOfIndices>;

  const auto& color = GetColor();
  const VertexArray vertices = {
      Vertex{
          .pos = pos,
          .color = color,
      },
      Vertex{
          .pos = glm::vec2(pos.x + size.x, pos.y),
          .color = color,
      },
      Vertex{
          .pos = glm::vec2(pos.x + size.x, pos.y + size.y),
          .color = color,
      },
      Vertex{
          .pos = glm::vec2(pos.x, pos.y + size.y),
          .color = color,
      },
  };
  const IndexArray indices = {
      0, 1, 1, 2, 2, 3, 3, 0,
  };
  GetLines()->Push(vertices, indices);
}

void RenderContext::DrawLine(const glm::vec2 start, const glm::vec2 end) {
  static constexpr const auto kTotalNumberOfVertices = 2;
  using VertexArray = TemplateVertexArray<kTotalNumberOfVertices>;

  static constexpr const auto kTotalNumberOfIndices = 2;
  using IndexArray = TemplateIndexArray<kTotalNumberOfIndices>;

  const auto& color = GetColor();
  const VertexArray vertices = {
      Vertex{
          .pos = start,
          .color = color,
      },
      Vertex{
          .pos = end,
          .color = color,
      },
  };
  const IndexArray indices = {
      0,
      1,
  };
  GetLines()->Push(vertices, indices);
}

void RenderContext::FillRect(const glm::vec2 pos, const glm::vec2 size) {
  static constexpr const auto kTotalNumberOfVertices = 4;
  using VertexArray = TemplateVertexArray<kTotalNumberOfVertices>;

  static constexpr const auto kTotalNumberOfIndices = 6;
  using IndexArray = TemplateIndexArray<kTotalNumberOfIndices>;

  const auto& color = GetColor();
  const VertexArray vertices = {
      Vertex{
          .pos = pos,
          .color = color,
      },
      Vertex{
          .pos = glm::vec2(pos.x + size.x, pos.y),
          .color = color,
      },
      Vertex{
          .pos = glm::vec2(pos.x + size.x, pos.y + size.y),
          .color = color,
      },
      Vertex{
          .pos = glm::vec2(pos.x, pos.y + size.y),
          .color = color,
      },
  };
  const IndexArray indices = {
      0, 1, 3, 1, 2, 3,
  };
  GetTriangles()->Push(vertices, indices);
}

void RenderContext::ClipRect(const glm::vec2 pos, const glm::vec2 size) {
  Flush();
  int gl_y = GetOwner()->GetResolution().y - (int)(pos.y + size.y);  // flip: top-left → GL bottom-left
  glEnable(GL_SCISSOR_TEST);
  glScissor((int)pos.x, gl_y, (int)size.x, (int)size.y);
}

void RenderContext::ClipReset() {
  Flush();
  glDisable(GL_SCISSOR_TEST);
}

}  // namespace kura

#endif  // KURA_RENDER_TARGET_OPENGL
