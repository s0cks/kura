#include "lsp/json.h"
#include "lsp/server/initialize.h"
#include "lsp/uri.h"

#include "common.h"
#include "lsp/server/server.h"

namespace kura::lsp {
void InitializeResult::operator>>(json_t* doc) const {
  KASSERT(doc);

  json_t* info_node = json_object();
  if (info_node) {
    info >> info_node;
    json_object_set_new(doc, "serverInfo", info_node);
  }

  json_t* caps_node = json_object();
  if (caps_node) {
    capabilities >> caps_node;
    json_object_set_new(doc, "capabilities", caps_node);
  }
}

void Server::HandleInitialize(const Request& req) {
  // The folders of the workspace are where `workspace/symbol` looks for files
  // that aren't open.
  std::vector<std::string> roots{};
  const auto add_root = [&](const std::string& uri) {
    if (const auto path = UriToPath(uri))
      roots.push_back(*path);
  };
  if (req.params) {
    if (const auto* folders = json_object_get(req.params, "workspaceFolders"); json_is_array(folders)) {
      size_t index = 0;
      json_t* folder = nullptr;
      json_array_foreach(folders, index, folder) {
        add_root(json::GetString(folder, "uri"));
      }
    }
    if (roots.empty())
      add_root(json::GetString(req.params, "rootUri"));
  }
  modules_.SetRoots(std::move(roots));

  return Send(req.id, InitializeResult{
                          .info = info_,
                          .capabilities = caps_,
                      });
}
}  // namespace kura::lsp
