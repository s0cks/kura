#include "lsp/keyword_completion.h"

#include <algorithm>
#include <cctype>

namespace kura::lsp {
static inline auto IsWordChar(const char c) -> bool {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
}

auto GetWordAtCursor(const std::string_view text, uint32_t offset) -> WordAtCursor {
  offset = std::min<uint32_t>(offset, static_cast<uint32_t>(text.size()));

  auto begin = offset;
  while (begin > 0 && IsWordChar(text[begin - 1]))
    begin--;

  auto at_line_start = true;
  for (auto idx = begin; idx > 0; idx--) {
    const auto c = text[idx - 1];
    if (c == '\n')
      break;
    if (c != ' ' && c != '\t' && c != '\r') {
      at_line_start = false;
      break;
    }
  }

  return WordAtCursor{
      .begin = begin,
      .end = offset,
      .text = text.substr(begin, offset - begin),
      .at_line_start = at_line_start,
  };
}

auto IsTopLevel(const std::string_view text, uint32_t offset) -> bool {
  offset = std::min<uint32_t>(offset, static_cast<uint32_t>(text.size()));

  int depth = 0;
  for (uint32_t idx = 0; idx < offset; idx++) {
    switch (text[idx]) {
      case '/':
        if (idx + 1 < offset && text[idx + 1] == '/') {
          while (idx < offset && text[idx] != '\n')
            idx++;
        }
        break;
      case '"':
        for (idx++; idx < offset && text[idx] != '"'; idx++) {
          if (text[idx] == '\\')
            idx++;
        }
        break;
      case '(':
      case '[':
      case '{':
        depth++;
        break;
      case ')':
      case ']':
      case '}':
        if (depth > 0)
          depth--;
        break;
      default:
        break;
    }
  }
  return depth == 0;
}

namespace {
struct DeclarationSnippet {
  std::string_view keyword;
  std::string_view detail;
  std::string_view body;
};

// Tab stops walk through the parts someone has to fill in. `$0` is where the
// cursor ends up. Record types use kebab-case property names and a trailing
// comma, as in the docs.
constexpr DeclarationSnippet kDeclarationSnippets[] = {
    {
        "fn",
        "Function declaration",
        "fn ${1:name}(${2}): ${3:Type} =>\n\t$0",
    },
    {
        "type",
        "Record type declaration",
        "type ${1:Name} = {\n\t$0\n}\n",
    },
    {
        "type",
        "Union type declaration",
        "type ${1:Name} = ${2:Type1} | ${3:Type2}\n",
    },
};
}  // namespace

auto GetDeclarationCompletions(const Document& doc, const uint32_t offset) -> std::vector<CompletionItem> {
  const auto& text = doc.GetContents();
  const auto word = GetWordAtCursor(text, offset);
  if (!word.at_line_start || !IsTopLevel(text, word.begin))
    return {};

  const auto range = Range{
      .start = doc.ToPosition(word.begin),
      .end = doc.ToPosition(word.end),
  };

  std::vector<CompletionItem> items{};
  for (const auto& snippet : kDeclarationSnippets) {
    if (!snippet.keyword.starts_with(word.text))
      continue;

    items.push_back(CompletionItem{
        .kind = CompletionItem::kSnippet,
        .label = std::string(snippet.keyword),
        .detail = std::string(snippet.detail),
        .preselect = word.text == snippet.keyword,
        .sort = std::string(word.text == snippet.keyword ? "0" : "1") + std::string(snippet.keyword),
        .insertText = std::string(snippet.body),
        .insertTextFormat = CompletionItem::kSnippetFormat,
        .textEdit =
            TextEdit{
                .range = range,
                .newText = std::string(snippet.body),
            },
    });
  }
  return items;
}
}  // namespace kura::lsp
