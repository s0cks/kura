#ifndef KURA_TESTS_GOLDEN_IMAGE_H
#define KURA_TESTS_GOLDEN_IMAGE_H

#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include "renderer/renderer_image.h"

namespace kura::test {
// Whether the image has the same pixels as golden/<name>.png (every channel
// within `tolerance`). When it doesn't, the failure says how many pixels
// differ, and <name>.actual.png (what was drawn) and <name>.diff.png (where it
// differs, in red) are written next to the test binary.
//
//   EXPECT_TRUE(MatchesGoldenImage("scene", renderer::ImageFromRenderer(r)));
//
// KURA_UPDATE_GOLDEN=1 writes the golden instead (look at it before committing
// it), and KURA_SAVE_IMAGES=<dir> saves every image checked, as .png and .jpg,
// so there is something to open and look at.
auto MatchesGoldenImage(const std::string& name, const renderer::Image& actual, uint8_t tolerance = 0)
    -> ::testing::AssertionResult;
}  // namespace kura::test

#endif  // KURA_TESTS_GOLDEN_IMAGE_H
