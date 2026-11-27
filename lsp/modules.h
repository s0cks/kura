#ifndef KURA_LSP_MODULES_H
#define KURA_LSP_MODULES_H

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "lsp/document.h"
#include "lsp/types.h"

namespace kura::lsp {
// Finds the `.kui` files that a `use "x.kui"` names, whether they are open in
// the editor or only on disk.
//
// A file that is open is the editor's copy, which may have changes that were
// never saved; a file that is not open is read from disk and read again when
// it changes there.
class ModuleResolver {
 private:
  struct Entry {
    std::unique_ptr<Document> doc{};
    std::filesystem::file_time_type mtime{};
  };

  DocumentStore& open_;
  std::unordered_map<std::string, Entry> loaded_{};
  std::vector<std::string> roots_{};

 public:
  explicit ModuleResolver(DocumentStore& open_documents) :
    open_(open_documents) {}

  // The folders of the workspace, as paths.
  void SetRoots(std::vector<std::string> roots) {
    roots_ = std::move(roots);
  }

  auto GetRoots() const -> const std::vector<std::string>& {
    return roots_;
  }

  // The path a `use` in `from` names. Empty when it isn't a `.kui` file that
  // can be told from the text alone.
  static auto ResolvePath(const Document& from, const std::string& header) -> std::filesystem::path;

  // The document at `path`, or null when there is none.
  auto Load(const std::filesystem::path& path) -> Document*;

  // The module the declaration `use_decl` in `from` imports.
  auto LoadUse(const Document& from, const syntax::UseDecl& use_decl) -> Document*;

  // Every `.kui` file under the workspace folders, up to a limit.
  auto FindSourceFiles(size_t limit = 2000) const -> std::vector<std::filesystem::path>;
};

// The declaration in another file that `ref` (`hp.percent`) names.
struct QualifiedTarget {
  Document* doc = nullptr;
  const Decl* decl = nullptr;
};
auto ResolveQualified(ModuleResolver& modules, Document& from, const QualifiedRef& ref) -> std::optional<QualifiedTarget>;

// The declarations a module offers: its top level.
auto GetTopLevelDecls(Document& module, Ns ns) -> std::vector<const Decl*>;
}  // namespace kura::lsp

#endif  // KURA_LSP_MODULES_H
