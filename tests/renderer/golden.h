#ifndef KURA_TESTS_GOLDEN_H
#define KURA_TESTS_GOLDEN_H

#include <string>

#include <gtest/gtest.h>

namespace kura::test {
// Whether `actual` is the same as the text in golden/<name>. On a mismatch the
// failure says where the first different line is.
//
//   EXPECT_TRUE(MatchesGolden("scene.commands.txt", renderer.DumpCommands()));
//
// Run the tests with KURA_UPDATE_GOLDEN=1 to write the goldens instead (and
// look at the diff before committing it).
auto MatchesGolden(const std::string& name, const std::string& actual) -> ::testing::AssertionResult;
}  // namespace kura::test

#endif  // KURA_TESTS_GOLDEN_H
