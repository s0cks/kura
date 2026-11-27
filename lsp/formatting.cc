#include "lsp/formatting.h"

#include <filesystem>
#include <algorithm>
#include <fstream>
#include <sstream>

#include "formatter/formatter.h"
#include "frontend/syntax/ast_printer.h"

namespace kura::lsp {
namespace {
// `kurafmt.yaml` and friends, in the directory of the file or the nearest one above.
auto LoadConfig(const std::string& uri, fmt::Config& config) -> void {
  namespace fs = std::filesystem;
  if (!uri.starts_with("file://"))
    return;

  // The JSON reader is not written yet, so only YAML configurations count.
  static constexpr const char* kSources[] = {"kurafmt.yaml", "kurafmt.yml", ".kurafmt.yaml", ".kurafmt.yml"};

  std::error_code ec{};
  for (auto dir = fs::path(uri.substr(7)).parent_path(); !dir.empty(); dir = dir.parent_path()) {
    for (const auto* name : kSources) {
      std::ifstream file(dir / name, std::ios::in | std::ios::binary);
      if (!file.is_open())
        continue;

      const std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
      if (fmt::ParseConfigYaml(data, config))
        return;
    }
    if (dir == dir.parent_path())
      break;
  }
}

auto SplitLines(const std::string& text) -> std::vector<std::string> {
  std::vector<std::string> lines{};
  size_t begin = 0;
  while (begin <= text.size()) {
    const auto end = text.find('\n', begin);
    if (end == std::string::npos) {
      lines.push_back(text.substr(begin));
      break;
    }
    lines.push_back(text.substr(begin, end - begin + 1));
    begin = end + 1;
  }
  return lines;
}
}  // namespace

auto FormatDocument(Document& doc, const FormattingOptions& options, const std::optional<Range>& range)
    -> std::expected<std::vector<TextEdit>, std::string> {
  if (!doc.EnsureTree())
    return std::unexpected("the file has not been parsed");

  for (const auto& d : doc.GetTree()->GetDiagnostics()) {
    if (d.IsError())
      return std::unexpected("the file has syntax errors; fix them before formatting");
  }

  fmt::Config config{};
  config.indent_size = options.tab_size;
  config.insert_final_newline = options.insert_final_newline;
  LoadConfig(doc.GetUri(), config);

  std::stringstream out{};
  fmt::Formatter::Format(out, *doc.GetTree(), config);
  auto formatted = out.str();
  // One newline at the end of the file, not the formatter's idea of how many.
  while (!formatted.empty() && (formatted.back() == '\n' || formatted.back() == '\r'))
    formatted.pop_back();
  if (options.insert_final_newline && !formatted.empty())
    formatted += '\n';
  const auto& original = doc.GetContents();
  if (formatted == original)
    return std::vector<TextEdit>{};

  // The formatter does not know every form of the language yet, and what it does
  // not know it leaves out. Formatting is only allowed to change how the code looks,
  // so the result has to read back as the same tree; if it doesn't, the file is left as it is.
  const auto reparsed = syntax::ParseText(doc.GetUri(), formatted);
  const bool parses = reparsed && !std::any_of(reparsed->GetDiagnostics().begin(), reparsed->GetDiagnostics().end(),
                                               [](const auto& d) { return d.IsError(); });
  if (!parses || syntax::PrintTreeToString(*reparsed) != syntax::PrintTreeToString(*doc.GetTree())) {
    return std::unexpected(
        "the formatter does not support some of the syntax in this file yet, and would have changed what it means");
  }

  // Replace only the lines that changed, so the editor keeps the cursor, the
  // folds and the undo history of everything else.
  const auto before = SplitLines(original);
  const auto after = SplitLines(formatted);
  size_t prefix = 0;
  while (prefix < before.size() && prefix < after.size() && before[prefix] == after[prefix])
    prefix++;
  size_t suffix = 0;
  while (suffix < before.size() - prefix && suffix < after.size() - prefix &&
         before[before.size() - 1 - suffix] == after[after.size() - 1 - suffix])
    suffix++;

  std::string replacement{};
  for (auto i = prefix; i < after.size() - suffix; i++)
    replacement += after[i];

  const auto first_line = static_cast<int>(prefix);
  const auto end_line = static_cast<int>(before.size() - suffix);
  Range changed{Position{first_line, 0}, Position{end_line, 0}};
  // The last "line" of a file without a final newline has no end of its own.
  if (static_cast<size_t>(end_line) >= before.size())
    changed.end = doc.ToPosition(static_cast<uint32_t>(original.size()));

  if (range && (changed.end.line < range->start.line || changed.start.line > range->end.line))
    return std::vector<TextEdit>{};

  return std::vector<TextEdit>{TextEdit{.range = changed, .newText = replacement}};
}
}  // namespace kura::lsp
