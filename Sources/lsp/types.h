#ifndef KURA_LSP_TYPES_H
#define KURA_LSP_TYPES_H

#include <jansson.h>
#include <string>
#include <variant>
#include <vector>

namespace kura::lsp {
struct Position {
  int line;
  int character;
};

void ToJson(json_t* doc, const Position& rhs);
void FromJson(json_t* doc, Position& rhs);

struct Range {
  Position start;
  Position end;
};

void ToJson(json_t* doc, const Range& rhs);

struct MarkupContent {
  enum Kind {
    kPlainText,
    kMarkdown,
  };

  Kind kind;
  std::string value;
};

void ToJson(json_t* doc, const MarkupContent& rhs);

struct Hover {
  Range range;
  MarkupContent contents;
};

void ToJson(json_t* doc, const Hover& rhs);

struct TextDocument {
  std::string uri{};
  std::string text{};

  TextDocument() = default;
  TextDocument(json_t* doc);
  ~TextDocument() = default;
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
};

void ToJson(json_t* doc, const ServerInfo& rhs);

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
};

void ToJson(json_t* doc, const ServerCapabilities& rhs);
}  // namespace kura::lsp

#endif  // KURA_LSP_TYPES_H
