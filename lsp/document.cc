#include "lsp/document.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <sstream>

#include "frontend/syntax/ast.h"

namespace kura::lsp {
Document::Document(const URI uri, std::string contents) :
  uri_(std::move(uri)) {
  Update(std::move(contents));
}

void Document::Update(std::string content) {
  contents_ = std::move(content);
  version_++;
  source_ = std::make_shared<const syntax::SourceFile>(uri_, contents_);
}

auto Document::ToOffset(const Position& rhs) const -> uint32_t {
  if (rhs.line < 0)
    return 0;

  const auto line = static_cast<uint32_t>(rhs.line);
  if (line >= source_->GetNumberOfLines())
    return source_->GetSize();

  const auto span = source_->GetLineSpan(line);
  const auto& text = source_->GetText();
  auto target = static_cast<uint32_t>(std::max(rhs.character, 0));
  uint32_t offset = span.begin;
  uint32_t units = 0;
  while (offset < span.end && units < target) {
    const auto c = static_cast<unsigned char>(text[offset]);
    const uint32_t width = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    units += c >= 0xF0 ? 2 : 1;
    offset = std::min(offset + width, span.end);
  }
  return offset;
}

auto Document::ToPosition(const uint32_t offset) const -> Position {
  const auto pos = source_->GetPosition(offset);
  return Position{
      .line = static_cast<int>(pos.line),
      .character = static_cast<int>(source_->GetUtf16Column(offset)),
  };
}

auto Document::ToRange(const syntax::Span& span) const -> Range {
  return Range{
      .start = ToPosition(span.begin),
      .end = ToPosition(span.end),
  };
}

Document::~Document() = default;

void Document::GetSymbols(std::vector<Symbol>& rhs) const {
  const auto* root = GetRoot();
  if (!root)
    return;

  root->ForEachChild([this, &rhs](const syntax::Node* node) {
    if (auto* fn = node->As<syntax::FnDecl>()) {
      const auto start_pos = tree_->GetSource().GetPosition(fn->span.begin);
      const auto end_pos = tree_->GetSource().GetPosition(fn->span.end);
      Range range{
          .start =
              Position{
                  .line = static_cast<int>(start_pos.line),
                  .character = static_cast<int>(start_pos.column),
              },
          .end =
              Position{
                  .line = static_cast<int>(end_pos.line),
                  .character = static_cast<int>(end_pos.column),
              },
      };

      rhs.push_back(Symbol{
          .name = fn->name.text,
          .detail = "",
          .kind = Symbol::kFunctionSymbol,
          .range = range,
          .selectionRange = range,
      });
    }
  });
}

auto Document::ToString() const -> std::string {
  std::stringstream ss{};
  ss << "Document(";
  ss << "uri=" << GetUri();
  ss << ")";
  return ss.str();
}

DocumentStore::DocumentStore() = default;

DocumentStore::~DocumentStore() = default;

auto DocumentStore::Get(const URI& uri) -> Document* {
  std::shared_lock lock(mutex_);
  const auto pos = docs_.find(uri);
  if (pos != std::end(docs_))
    return pos->second;

  return nullptr;
}

void DocumentStore::Update(const URI& uri, std::string contents) {
  std::unique_lock lock(mutex_);
  const auto pos = docs_.find(uri);
  if (pos != std::end(docs_)) {
    pos->second->Update(std::move(contents));
    return;
  }

  docs_.insert({uri, new Document(uri, std::move(contents))});
}

void DocumentStore::Remove(const URI& uri) {
  std::unique_lock lock(mutex_);
  docs_.erase(uri);
}
}  // namespace kura::lsp
