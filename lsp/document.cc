#include "lsp/document.h"

#include <mutex>
#include <sstream>

namespace kura::lsp {
Document::Document(const URI uri, std::string contents) :
  uri_(std::move(uri)),
  contents_(contents) {}

Document::~Document() = default;

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
