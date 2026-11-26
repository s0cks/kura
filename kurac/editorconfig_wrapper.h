#ifndef KURA_EDITORCONFIG_WRAPPER_H
#define KURA_EDITORCONFIG_WRAPPER_H

#ifdef KURA_HAS_EDITORCONFIG

#include <editorconfig/editorconfig.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace kura {

struct EditorConfigHandleDeleter {
  void operator()(editorconfig_handle h) const noexcept {
    if (h) {
      editorconfig_handle_destroy(h);
    }
  }
};

using EditorConfigHandle = std::unique_ptr<std::remove_pointer_t<editorconfig_handle>, EditorConfigHandleDeleter>;

static inline std::unordered_map<std::string, std::string> parse_editorconfig(const std::filesystem::path& filepath) {
  std::cout << "parsing editorconfig...." << std::endl;

  EditorConfigHandle handle(editorconfig_handle_init());
  if (!handle) {
    throw std::runtime_error("Failed to initialize editorconfig handle");
  }

  std::filesystem::path abs_path = std::filesystem::weakly_canonical(std::filesystem::absolute(filepath));
  std::string path_str = abs_path.string();

  int err = editorconfig_parse(path_str.c_str(), handle.get());
  if (err != 0) {
    throw std::runtime_error(std::string("EditorConfig parse error: ") + editorconfig_get_error_msg(err));
  }

  // 4. Extract resolved key-value pairs
  std::unordered_map<std::string, std::string> properties;
  int count = editorconfig_handle_get_name_value_count(handle.get());
  properties.reserve(static_cast<size_t>(count));

  for (int i = 0; i < count; ++i) {
    const char* name = nullptr;
    const char* value = nullptr;
    editorconfig_handle_get_name_value(handle.get(), i, &name, &value);
    if (name && value) {
      properties.emplace(name, value);
    }
  }

  return properties;
}
}  // namespace kura

#else

#warning "no editorconfig support"

#endif  // KURA_HAS_EDITORCONFIG

#endif  // KURA_EDITORCONFIG_WRAPPER_H
