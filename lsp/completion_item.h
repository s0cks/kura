#ifndef KURA_LSP_COMPLETION_ITEM_H
#define KURA_LSP_COMPLETION_ITEM_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "lsp/types.h"

namespace kura::lsp {
#define FOR_EACH_COMPLETION_ITEM_KIND(V) \
  V(Text)                                \
  V(Method)                              \
  V(Function)                            \
  V(Constructor)                         \
  V(Field)                               \
  V(Variable)                            \
  V(Class)                               \
  V(Interface)                           \
  V(Module)                              \
  V(Property)                            \
  V(Unit)                                \
  V(Value)                               \
  V(Enum)                                \
  V(Keyword)                             \
  V(Snippet)                             \
  V(Color)                               \
  V(File)                                \
  V(Reference)                           \
  V(Folder)                              \
  V(EnumMember)                          \
  V(Constant)                            \
  V(Struct)                              \
  V(Event)                               \
  V(Operator)                            \
  V(TypeParameter)

struct CompletionItem {
  // clang-format off
  enum Kind : uint32_t {
    kInvalidCompletionItem = 0,
#define DEFINE_KIND(Name) k##Name,
    FOR_EACH_COMPLETION_ITEM_KIND(DEFINE_KIND)
#undef DEFINE_KIND
    kTotalNumberOfKinds,
  };
  // clang-format on

  enum InsertTextFormat : uint32_t {
    kNoInsertTextFormat = 0,
    kPlainTextFormat = 1,
    kSnippetFormat = 2,
  };

  Kind kind = kInvalidCompletionItem;
  std::string label{};
  std::string detail{};
  MarkupContent documentation{};
  bool preselect = false;
  std::string sort{};
  std::string filter{};
  std::string insertText{};
  InsertTextFormat insertTextFormat = kNoInsertTextFormat;
  // When set, the client replaces this range with `newText` instead of
  // inserting `insertText` at its own idea of the word under the cursor.
  std::optional<TextEdit> textEdit{};

  void operator>>(json_t* doc) const;
};

struct CompletionList {
  bool isIncomplete = false;
  std::vector<CompletionItem> items{};

  void operator>>(json_t* doc) const;
};
}  // namespace kura::lsp

#endif  // KURA_LSP_COMPLETION_ITEM_H
