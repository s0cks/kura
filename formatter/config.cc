#include "formatter/config.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

static inline auto Slurp(const std::string& filepath) -> std::optional<std::string> {
  std::ifstream file(filepath, std::ios::in | std::ios::binary);
  if (!file.is_open())
    return std::nullopt;

  return {std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>())};
}

namespace kura::fmt {
struct ConfigSource {
  enum Kind {
    kJson,
    kYaml,
  };

  Kind kind;
  std::string_view filename;

  auto exists() const -> bool {
    return fs::exists(filename);
  }

  auto Parse(Config& rhs) const -> bool {
    if (!exists())
      return false;

    // The parsers read the text of the file, not its name.
    const auto data = Slurp(std::string(filename));
    if (!data)
      return false;

    switch (kind) {
      case kJson:
        return ParseConfigJson(*data, rhs);

      case kYaml:
        return ParseConfigYaml(*data, rhs);
    }
    return false;
  }
};

static const std::vector<ConfigSource> kPotentialConfigSources = {
    // yaml
    {ConfigSource::kYaml, "kurafmt.yaml"},
    {ConfigSource::kYaml, "kurafmt.yml"},
    {ConfigSource::kYaml, ".kurafmt.yaml"},
    {ConfigSource::kYaml, ".kurafmt.yml"},

    // json
    {ConfigSource::kJson, "kurafmt.json"},
    {ConfigSource::kJson, ".kurafmt.json"},
};

auto ParseConfig(Config& config) -> bool {
  for (const auto& source : kPotentialConfigSources) {
    if (source.Parse(config))
      return true;
  }

  return false;
}
}  // namespace kura::fmt
