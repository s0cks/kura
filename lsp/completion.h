#ifndef KURA_LSP_COMPLETION_H
#define KURA_LSP_COMPLETION_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "lsp/completion_item.h"
#include "lsp/document.h"
#include "lsp/modules.h"

namespace kura::lsp {
// What can be typed at `offset`. Which kind of thing depends on where that
// is: after `#` an element, after `@` a directive, after `.` a field, inside
// `#tag(` a property of that tag, after `:` in a signature a type, at the top
// level a declaration, and in an expression a name that is in scope.
auto GetCompletions(Document& doc, uint32_t offset, ModuleResolver* modules = nullptr) -> std::vector<CompletionItem>;

// What the text around `offset` is, found without the tree, which is
// whatever the last parse made of a half-written line.
struct CompletionContext {
  enum Kind {
    kNone,
    kTopLevel,
    kElementTag,
    kDirective,
    kMember,
    kPropertyName,
    kPropertyValue,
    kType,
    kExpression,
    kHeaderPath,
  };

  Kind kind = kNone;
  // kMember: `a.b` in `a.b.|`. kPropertyName and kPropertyValue: the tag.
  std::vector<std::string> chain{};
  std::string tag{};
  // kPropertyValue: the property being given a value.
  std::string property{};
  // kPropertyName: properties already written inside the same parentheses.
  std::vector<std::string> present{};
  // kHeaderPath: what is typed of the path so far.
  std::string prefix{};
  uint32_t word_begin = 0;
  uint32_t word_end = 0;
};

auto GetCompletionContext(std::string_view text, uint32_t offset) -> CompletionContext;

// The call, `#element(` or `Variant(` whose parentheses are open at `offset`.
struct CallSite {
  // `a.b` in `a.b(`.
  std::vector<std::string> callee{};
  // `#tag(` and `set tag(`: `tag` is the element, and the arguments are properties.
  bool is_element = false;
  std::string tag{};
  uint32_t open = 0;
  // How many arguments come before the cursor.
  int active_index = 0;
  // The property being given a value, in `#tag(name: |`.
  std::string active_name{};
};

auto FindEnclosingCall(std::string_view text, uint32_t offset) -> std::optional<CallSite>;
}  // namespace kura::lsp

#endif  // KURA_LSP_COMPLETION_H
