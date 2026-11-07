#ifndef KURA_LSP_TYPES_H
#define KURA_LSP_TYPES_H

#include <jansson.h>
#include <string>
#include <variant>
#include <vector>

#include "frontend/syntax/parser.h"

namespace kura::lsp {
struct Position {
  int line;
  int character;

  void operator>>(json_t* rhs) const;

  operator syntax::Position() const {
    return syntax::Position{
        .line = static_cast<uint32_t>(line),
        .column = static_cast<uint32_t>(character),
    };
  }
};

void FromJson(json_t* doc, Position& rhs);

struct Range {
  Position start;
  Position end;

  void operator>>(json_t* rhs) const;
};

struct MarkupContent {
  enum Kind {
    kPlainText,
    kMarkdown,
  };

  Kind kind;
  std::string value;

  void operator>>(json_t* rhs) const;
};

struct Hover {
  Range range;
  MarkupContent contents;

  void operator>>(json_t* rhs) const;
};

static inline auto HoverForRange(const std::string contents, const Range range,
                                 const MarkupContent::Kind kind = MarkupContent::kPlainText) -> Hover {
  return Hover{
      .range = range,
      .contents =
          MarkupContent{
              .kind = kind,
              .value = std::move(contents),
          },
  };
}

static inline auto HoverForRange(const std::string contents, const Position start, const Position end,
                                 const MarkupContent::Kind kind = MarkupContent::kPlainText) -> Hover {
  return HoverForRange(std::move(contents), Range{.start = start, .end = end}, kind);
}

struct TextDocument {
  std::string uri{};
  std::string text{};

  auto operator<<(json_t* doc) -> TextDocument&;
};

struct WorkspaceFolder {
  std::string uri;
  std::string name;
};

struct InitializeParams {
  int processId;
  std::string rootUri;
  std::vector<WorkspaceFolder> workspaceFolders;
};

struct ServerInfo {
  std::string name;
  int version;

  void operator>>(json_t* rhs) const;
};

enum TextDocumentSync : uint8_t {
  kNoTextDocumentSync,
  kFullTextDocumentSync,
  kIncrementalTextDocumentSync,
};

#define FOR_EACH_PROVIDER_FLAG(V) \
  V(hoverProvider)                \
  V(documentSymbolProvider)       \
  V(definitionProvider)           \
  V(referenceProvider)            \
  V(codeActionProvider)           \
  V(completionProvider)

struct ServerCapabilities {
  TextDocumentSync textDocumentSync;
#define DEFINE_PROVIDER_FLAG(Name) bool Name;
  FOR_EACH_PROVIDER_FLAG(DEFINE_PROVIDER_FLAG)
#undef DEFINE_PROVIDER_FLAG

  void operator>>(json_t* rhs) const;
};
}  // namespace kura::lsp

#endif  // KURA_LSP_TYPES_H
