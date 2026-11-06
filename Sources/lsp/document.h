#ifndef KURA_LSP_DOCUMENT_H
#define KURA_LSP_DOCUMENT_H

#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace kura::lsp {
using URI = std::string;

class Document {
  friend class DocumentStore;

 private:
  URI uri_;
  std::string contents_{};

  inline void Update(std::string content) {
    contents_ = std::move(content);
  }

 public:
  Document(const URI uri, std::string content);
  Document(const Document& rhs) = delete;
  Document(Document&& rhs) = delete;
  ~Document();

  auto GetUri() const -> const URI& {
    return uri_;
  }

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
