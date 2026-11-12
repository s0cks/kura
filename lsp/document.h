#ifndef KURA_LSP_DOCUMENT_H
#define KURA_LSP_DOCUMENT_H

#include <shared_mutex>
#include <string>
#include <unordered_map>

#include "compiler/frontend/syntax/parser.h"
#include "lsp/symbol.h"
#include "lsp/types.h"
#include "lsp/uri.h"

namespace kura::lsp {
class Document {
  friend class DocumentStore;

 private:
  URI uri_;
  std::string contents_{};
  std::unique_ptr<syntax::SyntaxTree> tree_{};
  // Counts every change to `contents_`. `tree_version_` is the version the
  // tree was parsed from, so a tree older than the text can be told apart.
  uint64_t version_ = 0;
  uint64_t tree_version_ = 0;
  std::shared_ptr<const syntax::SourceFile> source_{};

  void Update(std::string content);

 public:
  Document(const URI uri, std::string content);
  Document(const Document& rhs) = delete;
  Document(Document&& rhs) = delete;
  ~Document();

  auto GetUri() const -> const URI& {
    return uri_;
  }

  auto GetContents() const -> const std::string& {
    return contents_;
  }

  auto GetTree() -> std::unique_ptr<syntax::SyntaxTree>& {
    return tree_;
  }

  auto GetTree() const -> const std::unique_ptr<syntax::SyntaxTree>& {
    return tree_;
  }

  auto GetVersion() const -> uint64_t {
    return version_;
  }

  auto GetTreeVersion() const -> uint64_t {
    return tree_version_;
  }

  // True when the tree was parsed from the text as it is now. Indexing runs on
  // a worker thread, so right after an edit the tree is still the old one.
  auto IsTreeCurrent() const -> bool {
    return tree_ && tree_version_ == version_;
  }

  // The current text with its line table. Unlike the tree's source, this one
  // is never behind the text.
  auto GetSource() const -> const syntax::SourceFile& {
    return *source_;
  }

  // LSP columns are UTF-16 code units; the source is indexed by byte.
  auto ToOffset(const Position& rhs) const -> uint32_t;
  auto ToPosition(uint32_t offset) const -> Position;
  auto ToRange(const syntax::Span& span) const -> Range;

  auto GetRoot() const -> const syntax::Module* {
    if (!tree_)
      return nullptr;

    return tree_->GetRoot();
  }

  auto GetNodeAtPos(const Position& rhs) const -> const syntax::Node* {
    const auto offset = tree_->GetSource().GetOffset(syntax::Position{
        .line = static_cast<uint32_t>(rhs.line),
        .column = static_cast<uint32_t>(rhs.character),
    });
    return syntax::FindNodeAt(tree_->GetRoot(), offset);
  }

  void SetSyntaxTree(std::unique_ptr<syntax::SyntaxTree>&& rhs, const uint64_t version) {
    tree_ = std::move(rhs);
    tree_version_ = version;
  }

  void GetSymbols(std::vector<Symbol>& rhs) const;
  auto ToString() const -> std::string;

  auto operator=(Document&& rhs) -> Document& = delete;
  auto operator=(const Document& rhs) -> Document& = delete;
};

class DocumentStore {
 private:
  std::shared_mutex mutex_{};
  std::unordered_map<URI, Document*> docs_{};

 public:
  DocumentStore();
  DocumentStore(DocumentStore&& rhs) = delete;
  DocumentStore(const DocumentStore& rhs) = delete;
  ~DocumentStore();

  auto Get(const URI& uri) -> Document*;
  void Update(const URI& uri, std::string content);
  void Remove(const URI& uri);

  auto operator=(DocumentStore&& rhs) -> DocumentStore& = delete;
  auto operator=(const DocumentStore& rhs) -> DocumentStore& = delete;
};
}  // namespace kura::lsp

#endif  // KURA_LSP_DOCUMENT_H
