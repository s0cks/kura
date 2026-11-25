#ifndef KURA_FMT_CONFIG_H
#define KURA_FMT_CONFIG_H

#include <iostream>
#include <string>

namespace kura::fmt {
struct Config {
  bool types_first;

  friend auto operator<<(std::ostream& stream, const Config& rhs) -> std::ostream& {
    stream << "Config{";
    stream << "types_first=" << (rhs.types_first ? "yes" : "no");
    stream << "}";
    return stream;
  }
};

auto ParseConfig(Config& config) -> bool;
auto ParseConfigYaml(const std::string_view data, Config& config) -> bool;
auto ParseConfigJson(const std::string_view data, Config& config) -> bool;
}  // namespace kura::fmt

#endif  // KURA_FMT_CONFIG_H
