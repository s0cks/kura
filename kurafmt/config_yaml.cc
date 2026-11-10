#include <iostream>
#include <yaml.h>

#include "config.h"

namespace kura::fmt {
auto ParseConfigYaml(const std::string_view data, Config& config) -> bool {
  std::string last{};
  std::string current{};

  yaml_event_t event{};
  yaml_parser_t parser{};

  if (!yaml_parser_initialize(&parser)) {
    std::cerr << "failed to initialize YAML parser." << std::endl;
    return false;
  }

  yaml_parser_set_input_string(&parser, reinterpret_cast<const unsigned char*>(data.data()), data.length());

  bool done = false;
  int indent = 0;
  while (!done) {
    if (!yaml_parser_parse(&parser, &event)) {
      std::cerr << "parse error: " << parser.error << std::endl;
      break;
    }

    switch (event.type) {
      case YAML_STREAM_END_EVENT:
        done = true;
        break;

      case YAML_MAPPING_START_EVENT:
        indent += 2;
        break;

      case YAML_MAPPING_END_EVENT:
        indent -= 2;
        break;

      case YAML_SCALAR_EVENT:
        current = std::string{reinterpret_cast<char*>(event.data.scalar.value)};
        std::cerr << "last: " << last << "; current: " << current << std::endl;
        if (last.empty()) {
          last = current;
          break;
        }

        if (last == "TypesFirst") {
          std::transform(std::begin(current), std::end(current), std::begin(current), [](const unsigned char c) {
            return std::tolower(c);
          });

          config.types_first = current == "true";
        } else {
          std::cerr << "invalid key: " << last << std::endl;
        }

        last.clear();
        break;

      default:
        break;
    }

    yaml_event_delete(&event);
  }

  yaml_parser_delete(&parser);
  return true;
}
}  // namespace kura::fmt
