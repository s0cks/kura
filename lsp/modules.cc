#include "lsp/modules.h"

#include <fstream>
#include <optional>
#include <sstream>

#include "lsp/uri.h"

namespace kura::lsp {
namespace fs = std::filesystem;

auto ModuleResolver::ResolvePath(const Document& from, const std::string& header) -> fs::path {
  if (!header.ends_with(".kui") || header.starts_with("std/"))
    return {};

  const auto from_path = UriToPath(from.GetUri());
  if (!from_path)
    return {};

  const fs::path target(header);
  const auto resolved = target.is_absolute() ? target : fs::path(*from_path).parent_path() / target;
  std::error_code ec{};
  auto canonical = fs::weakly_canonical(resolved, ec);
  return ec ? resolved.lexically_normal() : canonical;
}

auto ModuleResolver::Load(const fs::path& path) -> Document* {
  if (path.empty())
    return nullptr;

  // What the editor has open is what counts.
  if (auto* open = open_.Get(PathToUri(path.string())))
    return open;

  std::error_code ec{};
  if (!fs::is_regular_file(path, ec))
    return nullptr;
  const auto mtime = fs::last_write_time(path, ec);

  auto& entry = loaded_[path.string()];
  if (entry.doc && entry.mtime == mtime)
    return entry.doc.get();

  std::ifstream file(path, std::ios::in | std::ios::binary);
  if (!file.is_open())
    return nullptr;

  std::stringstream text{};
  text << file.rdbuf();
  entry.doc = std::make_unique<Document>(PathToUri(path.string()), text.str());
  entry.mtime = mtime;
  return entry.doc.get();
}

auto ModuleResolver::LoadUse(const Document& from, const syntax::UseDecl& use_decl) -> Document* {
  if (!use_decl.has_header)
    return nullptr;
  return Load(ResolvePath(from, use_decl.header));
}

auto ModuleResolver::FindSourceFiles(const size_t limit) const -> std::vector<fs::path> {
  std::vector<fs::path> files{};
  for (const auto& root : roots_) {
    std::error_code ec{};
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    for (const fs::recursive_directory_iterator end; it != end && !ec; it.increment(ec)) {
      const auto name = it->path().filename().string();
      if (it->is_directory(ec) && (name.starts_with(".") || name == "node_modules" || name == "build")) {
        it.disable_recursion_pending();
        continue;
      }
      if (it->is_regular_file(ec) && it->path().extension() == ".kui") {
        files.push_back(it->path());
        if (files.size() >= limit)
          return files;
      }
    }
  }
  return files;
}

auto GetTopLevelDecls(Document& module, const Ns ns) -> std::vector<const Decl*> {
  std::vector<const Decl*> result{};
  const auto* bindings = module.GetBindings();
  if (!bindings || bindings->GetScopes().empty())
    return result;

  for (const auto id : bindings->GetScopes().front().decls) {
    const auto& decl = bindings->GetDecl(id);
    if (GetNamespace(decl.kind) == ns && decl.kind != DeclKind::kAlias)
      result.push_back(&decl);
  }
  return result;
}

auto ResolveQualified(ModuleResolver& modules, Document& from, const QualifiedRef& ref) -> std::optional<QualifiedTarget> {
  const auto* bindings = from.GetBindings();
  if (!bindings)
    return std::nullopt;

  const auto* use = bindings->GetDecl(ref.alias).node ? bindings->GetDecl(ref.alias).node->As<syntax::UseDecl>() : nullptr;
  if (!use)
    return std::nullopt;

  auto* target = modules.LoadUse(from, *use);
  if (!target)
    return std::nullopt;

  for (const auto* decl : GetTopLevelDecls(*target, ref.ns)) {
    if (decl->name == ref.name)
      return QualifiedTarget{target, decl};
  }
  return std::nullopt;
}
}  // namespace kura::lsp
