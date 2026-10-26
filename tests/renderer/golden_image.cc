#include "golden_image.h"

#include <cstdlib>
#include <filesystem>

namespace kura::test {
namespace {
void SaveForLooking(const std::string& name, const renderer::Image& image) {
  const char* dir = std::getenv("KURA_SAVE_IMAGES");
  if (dir == nullptr)
    return;
  std::filesystem::create_directories(dir);
  renderer::SavePng(image, std::string(dir) + "/" + name + ".png");
  renderer::SaveJpeg(image, std::string(dir) + "/" + name + ".jpg");
}
}  // namespace

auto MatchesGoldenImage(const std::string& name, const renderer::Image& actual, const uint8_t tolerance)
    -> ::testing::AssertionResult {
  SaveForLooking(name, actual);
  const std::string path = std::string(KURA_TEST_GOLDEN_DIR) + "/" + name + ".png";
  if (std::getenv("KURA_UPDATE_GOLDEN") != nullptr) {
    return renderer::SavePng(actual, path) ? ::testing::AssertionSuccess()
                                           : ::testing::AssertionFailure() << "can't write golden " << path;
  }
  const auto expected = renderer::LoadPng(path);
  if (!expected) {
    return ::testing::AssertionFailure()
           << "missing or unreadable golden " << path << " (run with KURA_UPDATE_GOLDEN=1 to write it)";
  }
  const auto diff = renderer::Compare(*expected, actual, tolerance);
  if (diff.IsMatch())
    return ::testing::AssertionSuccess();

  const std::string out = std::string(KURA_TEST_OUTPUT_DIR) + "/" + name;
  renderer::SavePng(actual, out + ".actual.png");
  auto failure = ::testing::AssertionFailure() << "image " << name << " differs from its golden\n  ";
  if (!diff.same_size) {
    return failure << "size: golden " << expected->width << "x" << expected->height << ", actual " << actual.width
                   << "x" << actual.height;
  }
  renderer::SavePng(renderer::MakeDiffImage(*expected, actual, tolerance), out + ".diff.png");
  return failure << diff.differing_pixels << " pixel(s) differ, by up to " << static_cast<int>(diff.max_delta)
                 << "\n  wrote " << out << ".actual.png and " << out << ".diff.png";
}
}  // namespace kura::test
