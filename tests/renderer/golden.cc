#include "golden.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace kura::test {
auto MatchesGolden(const std::string& name, const std::string& actual) -> ::testing::AssertionResult {
  const std::string path = std::string(KURA_TEST_GOLDEN_DIR) + "/" + name;
  if (std::getenv("KURA_UPDATE_GOLDEN") != nullptr) {
    std::ofstream out(path, std::ios::binary);
    out << actual;
    return out ? ::testing::AssertionSuccess() : ::testing::AssertionFailure() << "can't write golden " << path;
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return ::testing::AssertionFailure()
           << "missing golden " << path << " (run with KURA_UPDATE_GOLDEN=1 to write it)";
  }
  std::ostringstream contents;
  contents << in.rdbuf();
  const auto expected = contents.str();
  if (expected == actual)
    return ::testing::AssertionSuccess();

  // these are small files: say where the first difference is
  std::istringstream want(expected);
  std::istringstream got(actual);
  std::string want_line;
  std::string got_line;
  for (int line = 1;; line++) {
    const auto has_want = static_cast<bool>(std::getline(want, want_line));
    const auto has_got = static_cast<bool>(std::getline(got, got_line));
    if (!has_want && !has_got)
      break;
    if (!has_want || !has_got || want_line != got_line) {
      return ::testing::AssertionFailure()
             << "golden " << name << " differs at line " << line << "\n  golden: " << (has_want ? want_line : "<end>")
             << "\n  actual: " << (has_got ? got_line : "<end>") << "\n  (KURA_UPDATE_GOLDEN=1 rewrites the goldens)";
    }
  }
  return ::testing::AssertionFailure() << "golden " << name << " differs (line endings?)";
}
}  // namespace kura::test
