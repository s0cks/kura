#ifndef KURA_LSP_TYPES_H
#define KURA_LSP_TYPES_H

#include <initializer_list>
#include <jansson.h>
#include <string>
#include <variant>
#include <vector>

#include "frontend/syntax/parser.h"

namespace kura::lsp {
struct Position {
  int line;
  int character;

  Position() = default;
  Position(const syntax::Position pos) :
    line(pos.line),
    character(pos.column) {}
  Position(const int l, const int c) :
    line(l),
    character(c) {}
  ~Position() = default;

  auto operator<<(const json_t* rhs) -> Position&;
  void operator>>(json_t* rhs) const;

  operator syntax::Position() const {
    return syntax::Position{
        .line = static_cast<uint32_t>(line),
        .column = static_cast<uint32_t>(character),
    };
  }

  friend auto operator<<(std::ostream& stream, const Position& rhs) -> std::ostream& {
    stream << "Position{";
    stream << "line=" << rhs.line << ", ";
    stream << "character=" << rhs.character;
    stream << "}";
    return stream;
  }
};

void FromJson(json_t* doc, Position& rhs);

struct Range {
  Position start;
  Position end;

  void operator>>(json_t* rhs) const;
  auto operator<<(const json_t* rhs) -> Range&;

  friend auto operator<<(std::ostream& stream, const Range& rhs) -> std::ostream& {
    stream << "Range{";
    stream << "start=" << rhs.start << ", ";
    stream << "end=" << rhs.end;
    stream << "}";
    return stream;
  }
};

struct TextEdit {
  Range range;
  std::string newText;

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

struct TextDocumentPosition {
  TextDocument textDocument;
  Position position;

  auto operator<<(json_t* doc) -> TextDocumentPosition&;
};

struct WorkspaceFolder {
  std::string uri;
  std::string name;
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
  V(referencesProvider)           \
  V(codeActionProvider)

struct CompletionProvider {
  bool resolveProvider;
  std::vector<std::string> triggerCharacters{};

  void operator>>(json_t* rhs) const;
};

struct ServerCapabilities {
  TextDocumentSync textDocumentSync{};
  CompletionProvider completionProvider{};

#define DEFINE_PROVIDER_FLAG(Name) bool Name;
  FOR_EACH_PROVIDER_FLAG(DEFINE_PROVIDER_FLAG)
#undef DEFINE_PROVIDER_FLAG

  void operator>>(json_t* rhs) const;
};
}  // namespace kura::lsp

#endif  // KURA_LSP_TYPES_H
